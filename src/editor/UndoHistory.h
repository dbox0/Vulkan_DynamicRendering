#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "EditTarget.h"
#include "EditorSelection.h"
#include "../reflect/BinaryArchive.h"
#include "../scene/SceneSnapshot.h"

// ============================================================================
// Undo / redo.
//
// STATE-BASED, NOT COMMAND-BASED
//   Nothing here knows what a light, a material or a gizmo is. A change is
//   recorded as the state of the objects it touched, before and after, as
//   reflection snapshots. Undo writes "before" back, redo writes "after".
//   Anything reflected is undoable without a line of code here.
//
//   === RECORD KINDS
//   Modify   an EditTarget's state: before -> after
//   Create   a node subtree came into existence   (undo destroys it)
//   Destroy  a node subtree went away              (undo recreates it)
//   Move     a node was relinked in the hierarchy  (undo relinks it back)
//   New features never add a kind. A reparent is Move + Modify (the local
//   transform that keeps the world position); a new component will be a
//   Modify on a new EditTarget kind.
//
//   === HOW CALLERS RECORD
//   begin()
//     touch(target)   BEFORE changing a reflected object; captures "before"
//
//     destroy(root)   destroys through the world, capturing the subtree first
//
//     created(root)   AFTER a new subtree is fully set up; snapshots it now.
//                     Changing it later in the same transaction needs
//                     touch(), like any other object.
//
//     moved(...)      AFTER relinking a node
//
//   commit()          captures every "after", drops what did not change,
//                     pushes one transaction or nothing if nothing changed
//
//   begin/commit nest; only the outermost pair makes a transaction.
//
//   Every record describes one step from the state the previous record left
//   behind.
//
//   INVARIANTS
//   * History and world move in lockstep. The history is only correct if
//     every change to recorded state goes through a transaction; the
//     editor's const panels and command stream are what guarantee that.
//   * Undo applies a transaction's records in reverse, redo in order. Each
//     record's inverse only needs the world to be in the state it was in
//     right after (or before) that record, which reverse order guarantees.
//   * A new non-empty transaction clears the redo stack. An empty one does
//     not: nothing changed, so redo is still valid.
//   * If the world ever refuses a step, the transaction is rolled back, the
//     whole history is cleared and the failure reported.
// ============================================================================

// Everything the history needs from the world. Node operations are the
// Scene's (see SceneUndoWorld); everything else goes through capture/apply.
class UndoWorld
{
public:
    virtual ~UndoWorld() = default;

    [[nodiscard]] virtual bool capture(const EditTarget &target, reflect::Blob &out) const = 0;
    [[nodiscard]] virtual bool apply(const EditTarget &target, std::span<const uint8_t> snapshot) = 0;

    [[nodiscard]] virtual bool placement(Guid node, NodePlacement &out) const = 0;
    [[nodiscard]] virtual bool moveNode(Guid node, const NodePlacement &to) = 0;
    [[nodiscard]] virtual bool captureSubtree(Guid root, SubtreeSnapshot &out) const = 0;
    [[nodiscard]] virtual bool restoreSubtree(const SubtreeSnapshot &snapshot) = 0;
    [[nodiscard]] virtual bool destroySubtree(Guid root) = 0;
};

class UndoHistory
{
public:
    struct Record
    {
        enum class Kind : uint8_t { Modify, Create, Destroy, Move };

        Kind kind = Kind::Modify;

        // Modify. If the target is destroyed later in the same transaction,
        // `after` is its state just before that destroy.
        EditTarget    target;
        reflect::Blob before;
        reflect::Blob after;

        // Create / Destroy.
        SubtreeSnapshot subtree;

        // Modify: "after" already taken (by a destroy of the target).
        bool captured = false;

        // Destroy (root) / Move.
        Guid          node;
        NodePlacement from;
        NodePlacement to;

        [[nodiscard]] size_t byteSize() const;
    };

    struct Transaction
    {
        std::string         name;
        std::vector<Record> records;
        EditorSelection     selectionBefore;
        EditorSelection     selectionAfter;
        size_t              bytes  = 0;
        uint64_t            serial = 0;
    };

    enum class Result : uint8_t
    {
        Done,
        Nothing,   // nothing to undo / redo
        Failed     // the world refused; history has been cleared
    };

    struct Outcome
    {
        Result          result = Result::Nothing;
        std::string     name;
        EditorSelection selection;   // what to select now
    };

    static constexpr size_t DefaultBudget = 256ull * 1024 * 1024;

    explicit UndoHistory(UndoWorld &world, size_t byteBudget = DefaultBudget)
        : m_world(world), m_budget(byteBudget) {}

    UndoHistory(const UndoHistory &) = delete;
    UndoHistory &operator=(const UndoHistory &) = delete;

    // ---- recording ---------------------------------------------------------

    void begin(std::string_view name, const EditorSelection &selection);

    // False if the target does not exist: do not change it.
    [[nodiscard]] bool touch(const EditTarget &target);

    // False if nothing was destroyed.
    [[nodiscard]] bool destroy(Guid root);

    void created(Guid root);
    void moved(Guid node, const NodePlacement &from, const NodePlacement &to);

    void commit(const EditorSelection &selection);

    [[nodiscard]] bool isOpen() const { return m_depth > 0; }

    // ---- navigation --------------------------------------------------------

    Outcome undo();
    Outcome redo();

    [[nodiscard]] bool canUndo() const { return !m_undo.empty(); }
    [[nodiscard]] bool canRedo() const { return !m_redo.empty(); }
    [[nodiscard]] const std::string &undoName() const;
    [[nodiscard]] const std::string &redoName() const;
    [[nodiscard]] size_t undoCount() const { return m_undo.size(); }
    [[nodiscard]] size_t redoCount() const { return m_redo.size(); }

    // ---- bookkeeping -------------------------------------------------------

    // "Saved" is a position in history. Undoing back to it makes the world
    // clean again; a branch that discards it makes it unreachable.
    void markSaved();
    [[nodiscard]] bool isDirty() const;

    void clear();

    [[nodiscard]] size_t bytes() const { return m_bytes; }
    void setBudget(size_t byteBudget);

    // Bumps whenever transactions are discarded (trimmed, redo cleared,
    // cleared). Resources kept alive for the history's sake can be
    // reconsidered when it changes.
    [[nodiscard]] uint64_t revision() const { return m_revision; }

    // Every record in both stacks, for finding which resources the history
    // still refers to.
    void forEachRecord(const std::function<void(const Record &)> &visit) const;

private:
    [[nodiscard]] bool undoRecord(const Record &record);
    [[nodiscard]] bool redoRecord(const Record &record);
    void trim();
    void discard(std::deque<Transaction> &stack);
    [[nodiscard]] uint64_t currentSerial() const;
    void requireIdle(const char *what) const;

    UndoWorld &m_world;

    std::deque<Transaction> m_undo;
    std::deque<Transaction> m_redo;

    Transaction                    m_open;
    std::unordered_set<EditTarget> m_touched;
    std::unordered_set<Guid>       m_created;   // every node in this transaction's Create records
    int                            m_depth = 0;

    size_t   m_budget      = DefaultBudget;
    size_t   m_bytes       = 0;
    uint64_t m_nextSerial  = 1;
    uint64_t m_savedSerial = 0;
    uint64_t m_revision    = 0;
};
