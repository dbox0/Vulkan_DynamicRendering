#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "EditorCommands.h"

// Turns property changes into BeginEdit / Modify / EndEdit runs, one run per
// user gesture, and keeps every other command outside those runs.

class EditRecorder
{
public:
    using HeldId = uint64_t;

    void modify(std::vector<EditorCommand> &out, const EditTarget &target,
                reflect::Blob snapshot, std::string_view name, HeldId held);

    void push(std::vector<EditorCommand> &out, EditorCommand command);

    void endFrame(std::vector<EditorCommand> &out, HeldId held);

    void close(std::vector<EditorCommand> &out);

    [[nodiscard]] bool isOpen() const { return m_open; }

private:
    bool     m_open   = false;
    HeldId   m_held   = 0;
    uint32_t m_editId = 0;
    uint32_t m_nextId = 1;
};

// The applier's half of the contract: validates a command stream against the
// rules above, across frames. A violation is a bug in the editor, never
// something a user can cause, and Application treats it as fatal.
class EditStreamChecker
{
public:
    // Empty on success, otherwise what was wrong.
    [[nodiscard]] std::string check(const EditorCommand &command);

    [[nodiscard]] bool isOpen() const { return m_openId != 0; }

private:
    uint32_t m_openId = 0;
};
