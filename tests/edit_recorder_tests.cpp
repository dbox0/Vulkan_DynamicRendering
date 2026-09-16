// EditRecorder grouping rules and the EditStreamChecker contract.

#include <random>
#include <string>
#include <vector>

#include "TestFramework.h"
#include "../src/editor/EditRecorder.h"

namespace
{
    using Kind = EditorCommand::Kind;

    const EditTarget kNodeA = EditTarget::forNode(Guid{ 0xA });
    const EditTarget kNodeB = EditTarget::forNode(Guid{ 0xB });
    const EditTarget kMat   = EditTarget::forMaterial(3);

    reflect::Blob blob(uint8_t tag) { return reflect::Blob{ tag, 1, 2, 3 }; }

    EditorCommand structural(Kind kind = Kind::DeleteNode)
    {
        EditorCommand cmd;
        cmd.kind = kind;
        cmd.node = Guid{ 0xA };
        return cmd;
    }

    std::vector<Kind> kinds(const std::vector<EditorCommand> &commands)
    {
        std::vector<Kind> out;
        for (const auto &cmd : commands) {
            out.push_back(cmd.kind);
        }
        return out;
    }

    // Runs a stream through a fresh checker; true if every command passes.
    bool wellFormed(const std::vector<EditorCommand> &commands, bool mustEndClosed = true)
    {
        EditStreamChecker checker;
        for (const auto &cmd : commands) {
            if (!checker.check(cmd).empty()) {
                return false;
            }
        }
        return !mustEndClosed || !checker.isOpen();
    }

    size_t count(const std::vector<EditorCommand> &commands, Kind kind)
    {
        size_t n = 0;
        for (const auto &cmd : commands) {
            n += cmd.kind == kind ? 1 : 0;
        }
        return n;
    }

    void testInstantEdit()
    {
        section("edit: instant");

        EditRecorder rec;
        std::vector<EditorCommand> out;

        rec.modify(out, kNodeA, blob(1), "Rename Node", 0);
        CHECK(rec.isOpen());                       // open until end of frame
        rec.endFrame(out, 0);
        CHECK(!rec.isOpen());

        CHECK((kinds(out) == std::vector<Kind>{ Kind::BeginEdit, Kind::Modify, Kind::EndEdit }));
        CHECK(out[0].name == "Rename Node");
        CHECK(out[0].editId != 0);
        CHECK(out[1].editId == out[0].editId && out[2].editId == out[0].editId);
        CHECK(out[1].target == kNodeA);
        CHECK(out[1].snapshot == blob(1));
        CHECK(wellFormed(out));

        // Nothing open: close and endFrame emit nothing.
        out.clear();
        rec.close(out);
        rec.endFrame(out, 0);
        rec.endFrame(out, 42);
        CHECK(out.empty());
    }

    void testSameFrameInstantEditsMerge()
    {
        section("edit: one click, several changes");

        EditRecorder rec;
        std::vector<EditorCommand> out;
        rec.modify(out, kNodeA, blob(1), "Assign", 0);
        rec.modify(out, kMat, blob(2), "Assign", 0);
        rec.endFrame(out, 0);

        CHECK(count(out, Kind::BeginEdit) == 1);
        CHECK(count(out, Kind::Modify) == 2);
        CHECK(wellFormed(out));
    }

    void testDragAcrossFrames()
    {
        section("edit: drag spans frames");

        EditRecorder rec;
        std::vector<EditorCommand> out;
        constexpr EditRecorder::HeldId slider = 77;

        for (int frame = 0; frame < 100; ++frame) {
            // Not every frame moves the value.
            if (frame % 3 != 1) {
                rec.modify(out, kNodeA, blob(static_cast<uint8_t>(frame)), "Move", slider);
            }
            rec.endFrame(out, slider);
            CHECK(rec.isOpen());
        }
        // Release: ImGui has already cleared the active item by end of frame.
        rec.endFrame(out, 0);
        CHECK(!rec.isOpen());

        CHECK(count(out, Kind::BeginEdit) == 1);
        CHECK(count(out, Kind::EndEdit) == 1);
        CHECK(count(out, Kind::Modify) == 67);
        CHECK(out.front().name == "Move");
        CHECK(out.back().kind == Kind::EndEdit);
        CHECK(wellFormed(out));
    }

    void testReleaseInSameFrameAsChange()
    {
        section("edit: checkbox (change + release in one frame)");

        // The widget was held when the change was submitted, and is gone by
        // the end of the frame. One edit, not an empty one plus an instant one.
        EditRecorder rec;
        std::vector<EditorCommand> out;
        rec.modify(out, kNodeA, blob(1), "Edit Light", 9);
        rec.endFrame(out, 0);

        CHECK((kinds(out) == std::vector<Kind>{ Kind::BeginEdit, Kind::Modify, Kind::EndEdit }));
        CHECK(wellFormed(out));
    }

    void testSwitchingWidgetsSplits()
    {
        section("edit: switching widgets never merges");

        EditRecorder rec;
        std::vector<EditorCommand> out;

        // Typing in field 5, then clicking straight into field 6: something is
        // held on every frame, but they are different edits.
        rec.modify(out, kNodeA, blob(1), "Rename Node", 5);
        rec.endFrame(out, 5);
        rec.modify(out, kMat, blob(2), "Edit Material", 6);
        rec.endFrame(out, 6);
        rec.endFrame(out, 0);

        CHECK((kinds(out) == std::vector<Kind>{ Kind::BeginEdit, Kind::Modify, Kind::EndEdit,
                                                Kind::BeginEdit, Kind::Modify, Kind::EndEdit }));
        CHECK(out[0].editId != out[3].editId);
        CHECK(out[3].name == "Edit Material");
        CHECK(wellFormed(out));

        // Focus moved to a widget that changes nothing: the old edit still
        // closes at the end of that frame.
        out.clear();
        rec.modify(out, kNodeA, blob(1), "Rename Node", 5);
        rec.endFrame(out, 5);
        rec.endFrame(out, 8);
        CHECK(!rec.isOpen());
        CHECK(wellFormed(out));

        // Instant change, then a held one in the same frame: two edits.
        out.clear();
        rec.modify(out, kNodeA, blob(1), "Drop", 0);
        rec.modify(out, kNodeB, blob(2), "Move", 4);
        rec.endFrame(out, 4);
        CHECK(count(out, Kind::BeginEdit) == 2);
        CHECK(rec.isOpen());
        rec.endFrame(out, 0);
        CHECK(wellFormed(out));
    }

    void testOtherCommandsCloseEdits()
    {
        section("edit: structural commands never land inside an edit");

        EditRecorder rec;
        std::vector<EditorCommand> out;

        rec.modify(out, kNodeA, blob(1), "Move", 3);
        rec.push(out, structural());
        CHECK(!rec.isOpen());
        // The drag goes on after the delete: a new edit.
        rec.modify(out, kNodeA, blob(2), "Move", 3);
        rec.endFrame(out, 0);

        CHECK((kinds(out) == std::vector<Kind>{ Kind::BeginEdit, Kind::Modify, Kind::EndEdit,
                                                Kind::DeleteNode,
                                                Kind::BeginEdit, Kind::Modify, Kind::EndEdit }));
        CHECK(wellFormed(out));

        // push() with nothing open is just an append.
        out.clear();
        rec.push(out, structural(Kind::CreateEmpty));
        CHECK((kinds(out) == std::vector<Kind>{ Kind::CreateEmpty }));
    }

    void testEditIdsAreUnique()
    {
        section("edit: ids are unique");

        EditRecorder rec;
        std::vector<EditorCommand> out;
        for (int i = 0; i < 1000; ++i) {
            rec.modify(out, kNodeA, blob(1), "x", 0);
            rec.endFrame(out, 0);
        }
        std::vector<uint32_t> ids;
        for (const auto &cmd : out) {
            if (cmd.kind == Kind::BeginEdit) {
                ids.push_back(cmd.editId);
            }
        }
        bool increasing = ids.size() == 1000;
        for (size_t i = 1; i < ids.size(); ++i) {
            increasing &= ids[i] > ids[i - 1];
        }
        CHECK(increasing);
    }

    void testChecker()
    {
        section("edit stream checker");

        const auto make = [](Kind kind, uint32_t id, EditTarget target = kNodeA,
                             reflect::Blob snap = blob(1)) {
            EditorCommand cmd;
            cmd.kind     = kind;
            cmd.editId   = id;
            cmd.target   = target;
            cmd.snapshot = std::move(snap);
            return cmd;
        };

        {
            EditStreamChecker c;
            CHECK(!c.check(make(Kind::Modify, 1)).empty());          // outside any edit
        }
        {
            EditStreamChecker c;
            CHECK(!c.check(make(Kind::EndEdit, 1)).empty());         // nothing open
        }
        {
            EditStreamChecker c;
            CHECK(!c.check(make(Kind::BeginEdit, 0)).empty());       // id 0
        }
        {
            EditStreamChecker c;
            CHECK(c.check(make(Kind::BeginEdit, 1)).empty());
            CHECK(c.isOpen());
            CHECK(!c.check(make(Kind::BeginEdit, 2)).empty());       // nested
        }
        {
            EditStreamChecker c;
            CHECK(c.check(make(Kind::BeginEdit, 1)).empty());
            CHECK(!c.check(make(Kind::Modify, 2)).empty());          // wrong edit
            CHECK(!c.check(make(Kind::Modify, 1, EditTarget{})).empty());          // no target
            CHECK(!c.check(make(Kind::Modify, 1, EditTarget::forNode({}))).empty()); // null guid
            CHECK(!c.check(make(Kind::Modify, 1, kNodeA, {})).empty());            // no snapshot
            CHECK(!c.check(structural()).empty());                   // interleaved
            CHECK(!c.check(make(Kind::EndEdit, 2)).empty());         // wrong edit
            CHECK(c.check(make(Kind::Modify, 1)).empty());
            CHECK(c.check(make(Kind::EndEdit, 1)).empty());
            CHECK(!c.isOpen());
            CHECK(c.check(structural()).empty());
        }
        {
            // State carries across calls, i.e. across frames.
            EditStreamChecker c;
            CHECK(c.check(make(Kind::BeginEdit, 5)).empty());
            for (int frame = 0; frame < 10; ++frame) {
                CHECK(c.check(make(Kind::Modify, 5)).empty());
            }
            CHECK(c.check(make(Kind::EndEdit, 5)).empty());
        }
    }

    void testRandomStreams()
    {
        section("edit: random operation sequences stay well formed");

        std::mt19937 rng(2024);
        bool allWellFormed = true;
        bool modifiesInside = true;
        bool endsClosed = true;

        for (int run = 0; run < 500; ++run) {
            EditRecorder rec;
            EditStreamChecker checker;
            std::vector<EditorCommand> frame;

            for (int step = 0; step < 200; ++step) {
                const auto held = static_cast<EditRecorder::HeldId>(rng() % 4);   // 0 = nothing
                switch (rng() % 5) {
                case 0:
                case 1:
                    rec.modify(frame, (rng() & 1) ? kNodeA : kMat, blob(1), "m", held);
                    break;
                case 2:
                    rec.push(frame, structural());
                    break;
                default:
                    rec.endFrame(frame, held);
                    // "Apply" the frame, as Application does.
                    for (const auto &cmd : frame) {
                        allWellFormed &= checker.check(cmd).empty();
                        if (cmd.kind == Kind::Modify) {
                            modifiesInside &= checker.isOpen();
                        }
                    }
                    frame.clear();
                    break;
                }
            }

            rec.close(frame);
            for (const auto &cmd : frame) {
                allWellFormed &= checker.check(cmd).empty();
            }
            endsClosed &= !checker.isOpen() && !rec.isOpen();
        }

        CHECK(allWellFormed);
        CHECK(modifiesInside);
        CHECK(endsClosed);
    }
}

void runEditRecorderTests()
{
    testInstantEdit();
    testSameFrameInstantEditsMerge();
    testDragAcrossFrames();
    testReleaseInSameFrameAsChange();
    testSwitchingWidgetsSplits();
    testOtherCommandsCloseEdits();
    testEditIdsAreUnique();
    testChecker();
    testRandomStreams();
}
