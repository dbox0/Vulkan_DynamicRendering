#include "UndoHistory.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "../common/Fatal.h"

namespace
{
    constexpr uint64_t UnreachableSerial = std::numeric_limits<uint64_t>::max();

    bool contains(const SubtreeSnapshot &subtree, Guid guid)
    {
        return std::any_of(subtree.nodes.begin(), subtree.nodes.end(),
                           [guid](const SubtreeSnapshot::Entry &e) { return e.guid == guid; });
    }
}

size_t UndoHistory::Record::byteSize() const
{
    return sizeof(Record) + before.size() + after.size() + subtree.byteSize();
}

// ---------------------------------------------------------------------------
// recording
// ---------------------------------------------------------------------------

void UndoHistory::begin(std::string_view name, const EditorSelection &selection)
{
    if (m_depth++ > 0) {
        return;     // nested: the outermost transaction's name wins
    }
    m_open = Transaction{};
    m_open.name            = name;
    m_open.selectionBefore = selection;
    m_touched.clear();
    m_created.clear();
}

bool UndoHistory::touch(const EditTarget &target)
{
    if (m_depth == 0) {
        fatalError("UndoHistory::touch outside a transaction");
    }
    if (m_touched.contains(target)) {
        // Already recorded. Still answer "does it exist": it may have been
        // destroyed since, earlier in this transaction.
        reflect::Blob probe;
        return m_world.capture(target, probe);
    }

    Record record;
    record.kind   = Record::Kind::Modify;
    record.target = target;
    if (!m_world.capture(target, record.before)) {
        return false;
    }
    m_open.records.push_back(std::move(record));
    m_touched.insert(target);
    return true;
}

bool UndoHistory::destroy(Guid root)
{
    if (m_depth == 0) {
        fatalError("UndoHistory::destroy outside a transaction");
    }

    Record record;
    record.kind = Record::Kind::Destroy;
    record.node = root;
    if (!m_world.captureSubtree(root, record.subtree)) {
        return false;
    }

    // A Modify whose "after" would be taken at commit has to take it now,
    // while its node still exists.
    for (Record &earlier : m_open.records) {
        if (earlier.kind == Record::Kind::Modify && !earlier.captured &&
            earlier.target.kind == EditTarget::Kind::Node &&
            contains(record.subtree, earlier.target.node)) {
            if (!m_world.capture(earlier.target, earlier.after)) {
                fatalError("UndoHistory::destroy: a node in the subtree could not be captured");
            }
            earlier.captured = true;
        }
    }

    if (!m_world.destroySubtree(root)) {
        fatalError("UndoHistory::destroy: world captured a subtree it then refused to destroy");
    }
    m_open.records.push_back(std::move(record));
    return true;
}

void UndoHistory::created(Guid root)
{
    if (m_depth == 0) {
        fatalError("UndoHistory::created outside a transaction");
    }

    Record record;
    record.kind = Record::Kind::Create;
    record.node = root;
    if (!m_world.captureSubtree(root, record.subtree)) {
        fatalError("UndoHistory::created: the node does not exist");
    }

    // Each node may enter the world through one Create record only;
    // otherwise redo would try to create it twice. (Creating a parent, then
    // a child, then calling created() for both is the way to get this wrong:
    // the parent's snapshot already holds the child.)
    for (const SubtreeSnapshot::Entry &entry : record.subtree.nodes) {
        if (!m_created.insert(entry.guid).second) {
            fatalError("UndoHistory::created: node " + entry.guid.toString() +
                       " is already part of another created subtree");
        }
    }

    m_open.records.push_back(std::move(record));
}

void UndoHistory::moved(Guid node, const NodePlacement &from, const NodePlacement &to)
{
    if (m_depth == 0) {
        fatalError("UndoHistory::moved outside a transaction");
    }
    Record record;
    record.kind     = Record::Kind::Move;
    record.node     = node;
    record.from     = from;
    record.to       = to;
    record.captured = true;
    m_open.records.push_back(std::move(record));
}

void UndoHistory::commit(const EditorSelection &selection)
{
    if (m_depth == 0) {
        fatalError("UndoHistory::commit without begin");
    }
    if (--m_depth > 0) {
        return;
    }

    std::vector<Record> &records = m_open.records;

    for (Record &record : records) {
        if (record.kind == Record::Kind::Modify && !record.captured) {
            // Everything that could have removed the target went through
            // destroy(), which captured it there.
            if (!m_world.capture(record.target, record.after)) {
                fatalError("UndoHistory::commit: a touched object vanished outside the history");
            }
            record.captured = true;
        }
    }

    std::erase_if(records, [](const Record &r) {
        switch (r.kind) {
        case Record::Kind::Modify: return r.after == r.before;
        case Record::Kind::Move:   return r.from == r.to;
        default:                   return false;
        }
    });

    Transaction transaction = std::move(m_open);
    m_open = Transaction{};
    m_touched.clear();
    m_created.clear();

    if (transaction.records.empty()) {
        return;     // nothing changed; redo stays valid
    }

    discard(m_redo);

    transaction.selectionAfter = selection;
    transaction.serial         = m_nextSerial++;
    transaction.bytes          = sizeof(Transaction) + transaction.name.size();
    for (const Record &record : transaction.records) {
        transaction.bytes += record.byteSize();
    }

    m_bytes += transaction.bytes;
    m_undo.push_back(std::move(transaction));
    trim();
}

// ---------------------------------------------------------------------------
// navigation

bool UndoHistory::undoRecord(const Record &record)
{
    switch (record.kind) {
    case Record::Kind::Modify:  return m_world.apply(record.target, record.before);
    case Record::Kind::Create:  return m_world.destroySubtree(record.subtree.root());
    case Record::Kind::Destroy: return m_world.restoreSubtree(record.subtree);
    case Record::Kind::Move:    return m_world.moveNode(record.node, record.from);
    }
    return false;
}

bool UndoHistory::redoRecord(const Record &record)
{
    switch (record.kind) {
    case Record::Kind::Modify:  return m_world.apply(record.target, record.after);
    case Record::Kind::Create:  return m_world.restoreSubtree(record.subtree);
    case Record::Kind::Destroy: return m_world.destroySubtree(record.subtree.root());
    case Record::Kind::Move:    return m_world.moveNode(record.node, record.to);
    }
    return false;
}

UndoHistory::Outcome UndoHistory::undo()
{
    requireIdle("undo");
    if (m_undo.empty()) {
        return {};
    }

    const Transaction &transaction = m_undo.back();
    const size_t       count       = transaction.records.size();

    for (size_t i = count; i-- > 0;) {
        if (!undoRecord(transaction.records[i])) {
            // Put back what this undo already reverted, then give up on the
            // history: it no longer describes the world.
            for (size_t j = i + 1; j < count; ++j) {
                (void)redoRecord(transaction.records[j]);
            }
            Outcome failed{ Result::Failed, transaction.name, transaction.selectionAfter };
            clear();
            return failed;
        }
    }

    Outcome done{ Result::Done, transaction.name, transaction.selectionBefore };
    m_redo.push_back(std::move(m_undo.back()));
    m_undo.pop_back();
    return done;
}

UndoHistory::Outcome UndoHistory::redo()
{
    requireIdle("redo");
    if (m_redo.empty()) {
        return {};
    }

    const Transaction &transaction = m_redo.back();
    const size_t       count       = transaction.records.size();

    for (size_t i = 0; i < count; ++i) {
        if (!redoRecord(transaction.records[i])) {
            for (size_t j = i; j-- > 0;) {
                (void)undoRecord(transaction.records[j]);
            }
            Outcome failed{ Result::Failed, transaction.name, transaction.selectionBefore };
            clear();
            return failed;
        }
    }

    Outcome done{ Result::Done, transaction.name, transaction.selectionAfter };
    m_undo.push_back(std::move(m_redo.back()));
    m_redo.pop_back();
    return done;
}

const std::string &UndoHistory::undoName() const
{
    static const std::string none;
    return m_undo.empty() ? none : m_undo.back().name;
}

const std::string &UndoHistory::redoName() const
{
    static const std::string none;
    return m_redo.empty() ? none : m_redo.back().name;
}

// ---------------------------------------------------------------------------
// bookkeeping
uint64_t UndoHistory::currentSerial() const
{
    return m_undo.empty() ? 0 : m_undo.back().serial;
}

void UndoHistory::markSaved()
{
    requireIdle("markSaved");
    m_savedSerial = currentSerial();
}

bool UndoHistory::isDirty() const
{
    return currentSerial() != m_savedSerial;
}

void UndoHistory::clear()
{
    requireIdle("clear");
    const bool wasClean = !isDirty();
    m_undo.clear();
    m_redo.clear();
    m_bytes = 0;
    ++m_revision;
    // Serial 0 now means "this point", which is only the saved state if the
    // world was clean when the history went away.
    m_savedSerial = wasClean ? 0 : UnreachableSerial;
}

void UndoHistory::setBudget(size_t byteBudget)
{
    m_budget = byteBudget;
    trim();
}

void UndoHistory::discard(std::deque<Transaction> &stack)
{
    if (stack.empty()) {
        return;
    }
    for (const Transaction &transaction : stack) {
        m_bytes -= transaction.bytes;
    }
    stack.clear();
    ++m_revision;
}

void UndoHistory::trim()
{
    // The newest step always stays
    while (m_bytes > m_budget && m_undo.size() > 1) {
        m_bytes -= m_undo.front().bytes;
        m_undo.pop_front();
        ++m_revision;
    }
}

void UndoHistory::forEachRecord(const std::function<void(const Record &)> &visit) const
{
    for (const auto *stack : { &m_undo, &m_redo }) {
        for (const Transaction &transaction : *stack) {
            for (const Record &record : transaction.records) {
                visit(record);
            }
        }
    }
}

void UndoHistory::requireIdle(const char *what) const
{
    if (m_depth > 0) {
        fatalError(std::string("UndoHistory::") + what + " while a transaction is open");
    }
}
