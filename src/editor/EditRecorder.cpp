#include "EditRecorder.h"

#include <utility>

void EditRecorder::modify(std::vector<EditorCommand> &out, const EditTarget &target,
                          reflect::Blob snapshot, std::string_view name, HeldId held)
{
    if (m_open && m_held != held) {
        close(out);
    }

    if (!m_open) {
        m_open   = true;
        m_held   = held;
        m_editId = m_nextId++;
        if (m_nextId == 0) {
            m_nextId = 1;   // 0 means "no edit"; skip it on wrap
        }

        EditorCommand begin;
        begin.kind   = EditorCommand::Kind::BeginEdit;
        begin.editId = m_editId;
        begin.name   = name;
        out.push_back(std::move(begin));
    }

    EditorCommand change;
    change.kind     = EditorCommand::Kind::Modify;
    change.editId   = m_editId;
    change.target   = target;
    change.snapshot = std::move(snapshot);
    out.push_back(std::move(change));
}

void EditRecorder::push(std::vector<EditorCommand> &out, EditorCommand command)
{
    close(out);
    out.push_back(std::move(command));
}

void EditRecorder::endFrame(std::vector<EditorCommand> &out, HeldId held)
{
    if (m_open && (m_held == 0 || m_held != held)) {
        close(out);
    }
}

void EditRecorder::close(std::vector<EditorCommand> &out)
{
    if (!m_open) {
        return;
    }
    EditorCommand end;
    end.kind   = EditorCommand::Kind::EndEdit;
    end.editId = m_editId;
    out.push_back(std::move(end));

    m_open   = false;
    m_held   = 0;
    m_editId = 0;
}

std::string EditStreamChecker::check(const EditorCommand &command)
{
    using Kind = EditorCommand::Kind;

    switch (command.kind) {
    case Kind::BeginEdit:
        if (m_openId != 0) {
            return "BeginEdit " + std::to_string(command.editId) + " while edit " +
                   std::to_string(m_openId) + " is still open";
        }
        if (command.editId == 0) {
            return "BeginEdit with edit id 0";
        }
        m_openId = command.editId;
        return {};

    case Kind::Modify:
        if (m_openId == 0 || command.editId != m_openId) {
            return "Modify for edit " + std::to_string(command.editId) + " outside that edit";
        }
        if (!command.target.isValid()) {
            return "Modify without a valid target";
        }
        if (command.snapshot.empty()) {
            return "Modify without a snapshot";
        }
        return {};

    case Kind::EndEdit:
        if (m_openId == 0 || command.editId != m_openId) {
            return "EndEdit " + std::to_string(command.editId) + " does not match the open edit";
        }
        m_openId = 0;
        return {};

    default:
        if (m_openId != 0) {
            return "command " + std::to_string(static_cast<int>(command.kind)) +
                   " inside open edit " + std::to_string(m_openId);
        }
        return {};
    }
}
