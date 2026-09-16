// UndoHistory against the real Scene through SceneUndoWorld

#include <algorithm>
#include <cstdio>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include "TestFramework.h"
#include "../src/assets/Material.h"
#include "../src/editor/SceneUndoWorld.h"
#include "../src/editor/UndoHistory.h"
#include "../src/scene/Scene.h"

namespace
{

    struct SceneHolder
    {
        Scene scene;
    };

    class TestWorld final : private SceneHolder, public SceneUndoWorld
    {
    public:
        explicit TestWorld(size_t maxNodes = 256, size_t materialCount = 3)
            : SceneUndoWorld(SceneHolder::scene)
        {
            scene.initialize(maxNodes);
            materials.resize(materialCount);
            for (size_t i = 0; i < materials.size(); ++i) {
                materials[i].name = "mat" + std::to_string(i);
            }
        }

        using SceneHolder::scene;
        std::vector<Material> materials;
        std::vector<uint32_t> orphaned;

    protected:
        bool existsOther(const EditTarget &t) const override
        {
            return t.kind == EditTarget::Kind::Material && t.material >= 1 &&
                   t.material <= materials.size();
        }
        bool captureOther(const EditTarget &t, reflect::Blob &out) const override
        {
            if (!existsOther(t)) return false;
            out.clear();
            reflect::writeBinary(materials[t.material - 1], out);
            return true;
        }
        bool applyOther(const EditTarget &t, std::span<const uint8_t> snapshot) override
        {
            return existsOther(t) && reflect::readBinary(materials[t.material - 1], snapshot);
        }
        void onMeshesOrphaned(const std::vector<uint32_t> &meshes) override
        {
            orphaned.insert(orphaned.end(), meshes.begin(), meshes.end());
        }
    };


    std::string hex(const reflect::Blob &blob)
    {
        static constexpr char digits[] = "0123456789abcdef";
        std::string out;
        out.reserve(blob.size() * 2);
        for (const uint8_t b : blob) {
            out.push_back(digits[b >> 4]);
            out.push_back(digits[b & 0xF]);
        }
        return out;
    }

    void dumpNode(const Scene &scene, uint32_t id, int depth, std::string &out)
    {
        const Node &node = scene.getNode(id);
        const Guid parent = node.parentId ? scene.getNode(node.parentId).guid() : Guid{};
        out += std::string(static_cast<size_t>(depth) * 2, ' ') + node.guid().toString() +
               " p=" + parent.toString() + " " + hex(reflect::toBlob(node)) + "\n";
        for (uint32_t c = node.firstChildId; c != 0; c = scene.getNode(c).nextSiblingId) {
            dumpNode(scene, c, depth + 1, out);
        }
    }

    std::string dump(const TestWorld &world)
    {
        std::string out;
        for (const uint32_t root : world.scene.rootNodes()) {
            dumpNode(world.scene, root, 0, out);
        }
        for (const Material &m : world.materials) {
            out += "mat " + hex(reflect::toBlob(m)) + "\n";
        }
        out += "live " + std::to_string(world.scene.nodes().liveCount()) + "\n";
        return out;
    }


    bool integrity(const Scene &scene)
    {
        size_t reachable = 0;
        std::unordered_set<uint32_t> seen;
        std::vector<std::pair<uint32_t, uint32_t>> stack;   // node, expected parent
        for (const uint32_t root : scene.rootNodes()) {
            stack.emplace_back(root, 0);
        }
        while (!stack.empty()) {
            const auto [id, parent] = stack.back();
            stack.pop_back();
            if (!scene.isAlive(id) || !seen.insert(id).second) return false;
            const Node &node = scene.getNode(id);
            if (node.parentId != parent) return false;
            if (scene.findNode(node.guid()) != id) return false;
            ++reachable;
            for (uint32_t c = node.firstChildId; c != 0; c = scene.getNode(c).nextSiblingId) {
                stack.emplace_back(c, id);
            }
        }
        if (reachable != scene.nodes().liveCount()) return false;

        Scene probe = scene;
        if (probe.nodes().liveCount() < probe.maxNodes()) {
            const uint32_t id = probe.createNode(0, "probe");
            const std::vector<uint32_t> roots = probe.rootNodes();
            if (roots.empty() || roots.back() != id) return false;
        }
        return true;
    }

    Guid guidOf(const Scene &scene, uint32_t id) { return scene.getNode(id).guid(); }

    uint32_t make(Scene &scene, uint32_t parent, const char *name)
    {
        return scene.createNode(parent, name);
    }

    EditorSelection selectNode(Guid g)
    {
        EditorSelection s;
        s.mode = EditorSelection::Mode::Node;
        s.node = g;
        return s;
    }


    void testSceneStructureOps()
    {
        section("scene: placement, move, subtree capture/restore");

        TestWorld w;
        Scene &s = w.scene;
        const uint32_t a  = make(s, 0, "a");
        const uint32_t b  = make(s, 0, "b");
        const uint32_t c  = make(s, 0, "c");
        const uint32_t a1 = make(s, a, "a1");
        const uint32_t a2 = make(s, a, "a2");
        const uint32_t a21 = make(s, a2, "a21");

        CHECK(s.placementOf(a)  == (NodePlacement{ {}, {} }));
        CHECK(s.placementOf(c)  == (NodePlacement{ {}, guidOf(s, b) }));
        CHECK(s.placementOf(a2) == (NodePlacement{ guidOf(s, a), guidOf(s, a1) }));
        CHECK(integrity(s));

        // Moves: to the front, into a child list, and every refusal.
        CHECK(s.moveNode(c, NodePlacement{}));
        CHECK((s.rootNodes() == std::vector<uint32_t>{ c, a, b }));
        CHECK(integrity(s));
        CHECK(s.moveNode(c, NodePlacement{ guidOf(s, a), guidOf(s, a1) }));   // between a1 and a2
        CHECK(s.getNode(a1).nextSiblingId == c && s.getNode(c).nextSiblingId == a2);
        CHECK(integrity(s));

        const std::string before = dump(w);
        CHECK(!s.moveNode(a, NodePlacement{ guidOf(s, a21), {} }));          // cycle
        CHECK(!s.moveNode(a, NodePlacement{ guidOf(s, a), {} }));            // self
        CHECK(!s.moveNode(b, NodePlacement{ guidOf(s, a), guidOf(s, b) }));  // after itself
        CHECK(!s.moveNode(b, NodePlacement{ guidOf(s, a), guidOf(s, a21) })); // previous not a child of parent
        CHECK(!s.moveNode(b, NodePlacement{ Guid::generate(), {} }));        // unknown parent
        CHECK(!s.moveNode(b, NodePlacement{ {}, Guid::generate() }));        // unknown previous
        CHECK(dump(w) == before);

        // Move to the end of the root list keeps the tail cache right.
        CHECK(s.moveNode(c, NodePlacement{ {}, guidOf(s, b) }));
        CHECK(integrity(s));

        // Capture, destroy, restore: identical, same Guids, same order.
        const Guid aGuid = guidOf(s, a);
        SubtreeSnapshot snap;
        CHECK(s.captureSubtree(a, snap));
        CHECK(snap.nodes.size() == 4);
        CHECK(snap.root() == aGuid);
        CHECK(snap.nodes[1].parent == aGuid);

        const std::string full = dump(w);
        std::vector<uint32_t> orphans;
        s.destroyNode(a, orphans);
        CHECK(integrity(s));
        const std::string without = dump(w);

        // Invalid restores change nothing.
        SubtreeSnapshot bad = snap;
        bad.placement.previous = guidOf(s, b);            // b is a root, parent is root: valid...
        bad.placement.parent   = guidOf(s, c);            // ...but not a child of c
        CHECK(s.restoreSubtree(bad) == 0);
        bad = snap;
        bad.nodes[2].guid = bad.nodes[1].guid;            // repeated Guid
        CHECK(s.restoreSubtree(bad) == 0);
        bad = snap;
        bad.nodes[1].guid = guidOf(s, b);                 // Guid in use
        CHECK(s.restoreSubtree(bad) == 0);
        bad = snap;
        std::swap(bad.nodes[2], bad.nodes[3]);            // child before its parent
        CHECK(s.restoreSubtree(bad) == 0);
        bad = snap;
        bad.nodes[3].data.pop_back();                     // corrupt node snapshot
        CHECK(s.restoreSubtree(bad) == 0);
        bad = snap;
        bad.placement.parent = Guid::generate();          // unknown parent
        CHECK(s.restoreSubtree(bad) == 0);
        CHECK(dump(w) == without);
        CHECK(integrity(s));

        CHECK(s.restoreSubtree(snap) != 0);
        CHECK(dump(w) == full);
        CHECK(integrity(s));

        // Budget: a restore that does not fit fails whole.
        TestWorld small(3);
        const uint32_t r = make(small.scene, 0, "r");
        make(small.scene, r, "r1");
        make(small.scene, r, "r2");
        SubtreeSnapshot three;
        CHECK(small.scene.captureSubtree(r, three));
        CHECK(small.scene.restoreSubtree(three) == 0);    // Guids in use anyway...
        std::vector<uint32_t> o;
        small.scene.destroyNode(r, o);
        make(small.scene, 0, "blocker");
        CHECK(small.scene.restoreSubtree(three) == 0);    // ...and now: 1 + 3 > 3
        CHECK(small.scene.nodes().liveCount() == 1);
    }

    void testModify()
    {
        section("undo: modify");

        TestWorld w;
        UndoHistory h(w);
        const uint32_t n = make(w.scene, 0, "node");
        const Guid g = guidOf(w.scene, n);
        const std::string s0 = dump(w);

        h.begin("Move", selectNode(g));
        CHECK(h.isOpen());
        CHECK(h.touch(EditTarget::forNode(g)));
        w.scene.getNode(n).setTranslation({ 1, 2, 3 });
        CHECK(h.touch(EditTarget::forNode(g)));           // repeated touch is fine
        w.scene.getNode(n).setTranslation({ 4, 5, 6 });
        h.commit(selectNode(g));
        const std::string s1 = dump(w);

        CHECK(!h.isOpen());
        CHECK(h.canUndo() && !h.canRedo());
        CHECK(h.undoName() == "Move");

        auto out = h.undo();
        CHECK(out.result == UndoHistory::Result::Done);
        CHECK(out.name == "Move");
        CHECK(out.selection == selectNode(g));
        CHECK(dump(w) == s0);
        CHECK(h.canRedo() && h.redoName() == "Move");

        out = h.redo();
        CHECK(out.result == UndoHistory::Result::Done);
        CHECK(dump(w) == s1);
        CHECK(w.scene.getNode(n).getTranslation() == glm::vec3(4, 5, 6));

        // Materials are the same mechanism.
        h.begin("Edit Material", {});
        CHECK(h.touch(EditTarget::forMaterial(2)));
        w.materials[1].roughnessFactor = 0.125f;
        h.commit({});
        CHECK(h.undo().result == UndoHistory::Result::Done);
        CHECK(w.materials[1].roughnessFactor == 1.0f);
        CHECK(h.redo().result == UndoHistory::Result::Done);
        CHECK(w.materials[1].roughnessFactor == 0.125f);

        // Touching something that does not exist is refused, not recorded.
        h.begin("x", {});
        CHECK(!h.touch(EditTarget::forNode(Guid::generate())));
        CHECK(!h.touch(EditTarget::forMaterial(99)));
        h.commit({});
        CHECK(h.undoCount() == 2);

        // Nothing to undo / redo.
        UndoHistory empty(w);
        CHECK(empty.undo().result == UndoHistory::Result::Nothing);
        CHECK(empty.redo().result == UndoHistory::Result::Nothing);
        CHECK(empty.undoName().empty() && empty.redoName().empty());
    }

    void testNoOpKeepsRedo()
    {
        section("undo: no-op transactions vanish and keep redo");

        TestWorld w;
        UndoHistory h(w);
        const uint32_t n = make(w.scene, 0, "node");
        const Guid g = guidOf(w.scene, n);

        h.begin("Rename", {});
        CHECK(h.touch(EditTarget::forNode(g)));
        w.scene.getNode(n).name = "renamed";
        h.commit({});
        CHECK(h.undo().result == UndoHistory::Result::Done);
        CHECK(h.canRedo());
        const uint64_t revision = h.revision();

        // A drag that ends where it started.
        h.begin("Move", {});
        CHECK(h.touch(EditTarget::forNode(g)));
        w.scene.getNode(n).setTranslation({ 9, 9, 9 });
        w.scene.getNode(n).setTranslation({ 0, 0, 0 });
        h.commit({});
        // An empty transaction.
        h.begin("Nothing", {});
        h.commit({});
        // A move back to where it was.
        h.begin("Move", {});
        const NodePlacement here = w.scene.placementOf(n);
        h.moved(g, here, here);
        h.commit({});

        CHECK(h.undoCount() == 0);
        CHECK(h.canRedo());
        CHECK(h.revision() == revision);
        CHECK(h.redo().result == UndoHistory::Result::Done);
        CHECK(w.scene.getNode(n).name == "renamed");

        // A real change after an undo discards redo.
        CHECK(h.undo().result == UndoHistory::Result::Done);
        h.begin("Scale", {});
        CHECK(h.touch(EditTarget::forNode(g)));
        w.scene.getNode(n).setScale({ 2, 2, 2 });
        h.commit({});
        CHECK(!h.canRedo());
        CHECK(h.revision() > revision);
    }

    void testCreateDestroy()
    {
        section("undo: create and destroy");

        TestWorld w;
        UndoHistory h(w);
        Scene &s = w.scene;
        const uint32_t a = make(s, 0, "a");
        const uint32_t b = make(s, 0, "b");
        const uint32_t c = make(s, 0, "c");
        for (int i = 0; i < 3; ++i) {
            const uint32_t child = make(s, b, ("b" + std::to_string(i)).c_str());
            make(s, child, "leaf");
        }
        s.getNode(b).meshId = 42;
        const Guid bGuid = guidOf(s, b);
        const std::string s0 = dump(w);

        // Delete a middle root with a subtree.
        h.begin("Delete b", selectNode(bGuid));
        CHECK(h.destroy(bGuid));
        CHECK(!h.destroy(bGuid));                        // already gone
        h.commit({});
        const std::string s1 = dump(w);
        CHECK(s.findNode(bGuid) == 0);
        CHECK((w.orphaned == std::vector<uint32_t>{ 42 }));
        CHECK(integrity(s));

        auto out = h.undo();
        CHECK(out.result == UndoHistory::Result::Done);
        CHECK(out.selection == selectNode(bGuid));       // undoing a delete re-selects it
        CHECK(dump(w) == s0);                             // same Guids, same place, same data
        CHECK(integrity(s));
        const std::vector<uint32_t> roots = s.rootNodes();
        CHECK(roots.size() == 3 && roots[0] == a && roots[2] == c);

        CHECK(h.redo().result == UndoHistory::Result::Done);
        CHECK(dump(w) == s1);
        CHECK(integrity(s));
        CHECK(h.undo().result == UndoHistory::Result::Done);

        // Delete the first and the last root: the ends of the list.
        for (const uint32_t end : { a, c }) {
            const Guid g = guidOf(s, end);
            h.begin("Delete", {});
            CHECK(h.destroy(g));
            h.commit({});
            CHECK(integrity(s));
            CHECK(h.undo().result == UndoHistory::Result::Done);
            CHECK(dump(w) == s0);
            CHECK(integrity(s));
        }

        // Create, set up, then record. Undo removes it, redo brings it back
        // with the same Guid and data.
        h.begin("Create Light", {});
        const uint32_t light = s.createNode(s.findNode(bGuid), "light");
        s.getNode(light).lightType = LightType::Directional;
        s.getNode(light).setTranslation({ 0, 4, 0 });
        const Guid lightGuid = guidOf(s, light);
        h.created(lightGuid);
        h.commit(selectNode(lightGuid));
        const std::string s2 = dump(w);

        out = h.undo();
        CHECK(out.selection == EditorSelection{});
        CHECK(s.findNode(lightGuid) == 0);
        CHECK(dump(w) == s0);
        out = h.redo();
        CHECK(out.selection == selectNode(lightGuid));
        CHECK(dump(w) == s2);
        CHECK(s.getNode(s.findNode(lightGuid)).lightType == LightType::Directional);
        CHECK(integrity(s));
    }

    void testReparent()
    {
        section("undo: reparent = move + transform");

        TestWorld w;
        UndoHistory h(w);
        Scene &s = w.scene;
        const uint32_t p1 = make(s, 0, "p1");
        const uint32_t p2 = make(s, 0, "p2");
        const uint32_t x  = make(s, p1, "x");
        make(s, p1, "y");
        s.getNode(p2).setTranslation({ 10, 0, 0 });
        s.getNode(p2).setScale({ 2, 2, 2 });
        s.getNode(x).setTranslation({ 1, 2, 3 });
        s.nodes().updateTransforms(s.rootNodeId());
        const glm::mat4 worldBefore = s.nodes().worldMatrix(x);
        const std::string s0 = dump(w);

        const Guid xg = guidOf(s, x);
        h.begin("Reparent", {});
        const NodePlacement from = s.placementOf(x);
        CHECK(h.touch(EditTarget::forNode(xg)));
        CHECK(s.reparentNode(x, p2));
        h.moved(xg, from, s.placementOf(x));
        h.commit({});
        const std::string s1 = dump(w);
        CHECK(s0 != s1);

        s.nodes().updateTransforms(s.rootNodeId());
        const glm::mat4 worldAfter = s.nodes().worldMatrix(s.findNode(xg));
        CHECK(glm::all(glm::lessThan(glm::abs(worldAfter[3] - worldBefore[3]), glm::vec4(1e-4f))));

        CHECK(h.undo().result == UndoHistory::Result::Done);
        CHECK(dump(w) == s0);                  // back under p1, first, original local transform
        CHECK(integrity(s));
        CHECK(h.redo().result == UndoHistory::Result::Done);
        CHECK(dump(w) == s1);
        CHECK(integrity(s));
    }

    void testMixedTransactions()
    {
        section("undo: several records in one transaction");

        TestWorld w;
        UndoHistory h(w);
        Scene &s = w.scene;
        const uint32_t a = make(s, 0, "a");
        const uint32_t b = make(s, 0, "b");
        const uint32_t c = make(s, 0, "c");
        const uint32_t x = make(s, a, "x");
        const std::string s0 = dump(w);
        const Guid ag = guidOf(s, a), bg = guidOf(s, b), cg = guidOf(s, c), xg = guidOf(s, x);

        // Create R, move x under it, modify x, destroy R (taking x along),
        // create R's child separately first, and delete two siblings.
        h.begin("Mixed", {});
        const uint32_t r = s.createNode(0, "R");
        const Guid rg = guidOf(s, r);
        h.created(rg);
        const uint32_t rc = s.createNode(r, "Rchild");
        h.created(guidOf(s, rc));

        const NodePlacement from = s.placementOf(x);
        const NodePlacement to{ rg, guidOf(s, rc) };
        CHECK(s.moveNode(x, to));
        h.moved(xg, from, to);

        CHECK(h.touch(EditTarget::forNode(xg)));
        s.getNode(s.findNode(xg)).name = "x renamed";

        CHECK(h.destroy(rg));                 // takes Rchild and x with it
        CHECK(!h.touch(EditTarget::forNode(xg)));   // gone now, even though touched

        CHECK(h.destroy(bg));
        CHECK(h.destroy(cg));                 // b and c were adjacent
        CHECK(h.touch(EditTarget::forNode(ag)));
        s.getNode(s.findNode(ag)).name = "a renamed";
        h.commit({});

        const std::string s1 = dump(w);
        CHECK(integrity(s));
        CHECK(h.undoCount() == 1);

        CHECK(h.undo().result == UndoHistory::Result::Done);
        CHECK(dump(w) == s0);
        CHECK(integrity(s));
        CHECK(h.redo().result == UndoHistory::Result::Done);
        CHECK(dump(w) == s1);
        CHECK(integrity(s));
        CHECK(h.undo().result == UndoHistory::Result::Done);
        CHECK(dump(w) == s0);
    }

    void testNesting()
    {
        section("undo: nested begin/commit is one step");

        TestWorld w;
        UndoHistory h(w);
        const uint32_t n = make(w.scene, 0, "n");
        const Guid g = guidOf(w.scene, n);

        h.begin("Outer", selectNode(g));
        h.begin("Inner", {});
        CHECK(h.touch(EditTarget::forNode(g)));
        w.scene.getNode(n).name = "inner";
        h.commit({});
        CHECK(h.isOpen());
        CHECK(h.undoCount() == 0);
        h.begin("Inner 2", {});
        CHECK(h.touch(EditTarget::forMaterial(1)));
        w.materials[0].metallicFactor = 0.0f;
        h.commit({});
        h.commit({});

        CHECK(!h.isOpen());
        CHECK(h.undoCount() == 1);
        CHECK(h.undoName() == "Outer");
        CHECK(h.undo().selection == selectNode(g));
        CHECK(w.scene.getNode(n).name == "n" && w.materials[0].metallicFactor == 1.0f);
    }

    void testSavePoint()
    {
        section("undo: save point");

        TestWorld w;
        UndoHistory h(w);
        const uint32_t n = make(w.scene, 0, "n");
        const Guid g = guidOf(w.scene, n);
        const auto edit = [&](float x) {
            h.begin("Move", {});
            CHECK(h.touch(EditTarget::forNode(g)));
            w.scene.getNode(n).setTranslation({ x, 0, 0 });
            h.commit({});
        };

        CHECK(!h.isDirty());
        edit(1);
        CHECK(h.isDirty());
        h.markSaved();
        CHECK(!h.isDirty());
        edit(2);
        CHECK(h.isDirty());
        CHECK(h.undo().result == UndoHistory::Result::Done);
        CHECK(!h.isDirty());                  // back at the saved state
        CHECK(h.undo().result == UndoHistory::Result::Done);
        CHECK(h.isDirty());                   // before it
        CHECK(h.redo().result == UndoHistory::Result::Done);
        CHECK(!h.isDirty());

        // A no-op keeps it clean.
        h.begin("nothing", {});
        h.commit({});
        CHECK(!h.isDirty());

        // Branch away after undoing past the save: unreachable.
        CHECK(h.undo().result == UndoHistory::Result::Done);
        edit(3);
        CHECK(h.isDirty());
        CHECK(h.undo().result == UndoHistory::Result::Done);
        CHECK(h.isDirty());

        // Clearing while clean stays clean; while dirty stays dirty.
        h.markSaved();
        h.clear();
        CHECK(!h.isDirty());
        edit(4);
        h.clear();
        CHECK(h.isDirty());
        h.markSaved();
        CHECK(!h.isDirty());
    }

    void testBudget()
    {
        section("undo: memory budget");

        TestWorld w;
        UndoHistory h(w, 1);   // absurdly small: only the newest step survives
        const uint32_t n = make(w.scene, 0, "n");
        const Guid g = guidOf(w.scene, n);

        uint64_t revision = h.revision();
        for (int i = 1; i <= 50; ++i) {
            h.begin("Move", {});
            CHECK(h.touch(EditTarget::forNode(g)));
            w.scene.getNode(n).setTranslation({ static_cast<float>(i), 0, 0 });
            h.commit({});
            CHECK(h.undoCount() == 1);
        }
        CHECK(h.revision() > revision);
        CHECK(h.bytes() > 1);
        CHECK(h.undo().result == UndoHistory::Result::Done);
        CHECK(w.scene.getNode(n).getTranslation().x == 49.0f);
        CHECK(h.undo().result == UndoHistory::Result::Nothing);

        // Raising the budget keeps everything from then on.
        h.setBudget(UndoHistory::DefaultBudget);
        CHECK(h.redo().result == UndoHistory::Result::Done);
        for (int i = 0; i < 20; ++i) {
            h.begin("Move", {});
            CHECK(h.touch(EditTarget::forNode(g)));
            w.scene.getNode(n).setTranslation({ 100.0f + static_cast<float>(i), 0, 0 });
            h.commit({});
        }
        CHECK(h.undoCount() == 21);

        // Lowering it trims from the oldest end, and bytes stay consistent.
        revision = h.revision();
        const size_t all = h.bytes();
        h.setBudget(all / 2);
        CHECK(h.undoCount() < 21 && h.undoCount() >= 1);
        CHECK(h.bytes() <= all / 2);
        CHECK(h.revision() > revision);
        while (h.undo().result == UndoHistory::Result::Done) {}
        while (h.redo().result == UndoHistory::Result::Done) {}
        CHECK(w.scene.getNode(n).getTranslation().x == 119.0f);

        h.clear();
        CHECK(h.bytes() == 0 && h.undoCount() == 0 && h.redoCount() == 0);
    }

    void testFailureRollsBack()
    {
        section("undo: a world that no longer matches");

        TestWorld w;
        UndoHistory h(w);
        Scene &s = w.scene;
        const uint32_t a = make(s, 0, "a");
        const uint32_t b = make(s, 0, "b");
        const Guid ag = guidOf(s, a), bg = guidOf(s, b);

        // One transaction touching a material, then a (later-sabotaged) node,
        // then another material. Undo runs in reverse: material 2 is
        // reverted, then the node fails.
        h.begin("Edit", {});
        CHECK(h.touch(EditTarget::forMaterial(1)));
        w.materials[0].name = "one";
        CHECK(h.touch(EditTarget::forNode(ag)));
        s.getNode(a).name = "a2";
        CHECK(h.touch(EditTarget::forMaterial(2)));
        w.materials[1].name = "two";
        h.commit({});

        h.begin("Other", {});
        CHECK(h.touch(EditTarget::forNode(bg)));
        s.getNode(b).name = "b2";
        h.commit({});
        CHECK(h.undo().result == UndoHistory::Result::Done);   // one step on the redo stack

        // Behind the history's back.
        std::vector<uint32_t> orphans;
        s.destroyNode(a, orphans);
        const std::string sabotaged = dump(w);

        const auto out = h.undo();
        CHECK(out.result == UndoHistory::Result::Failed);
        CHECK(out.name == "Edit");
        CHECK(dump(w) == sabotaged);           // material 2 was put back
        CHECK(w.materials[1].name == "two");
        CHECK(!h.canUndo() && !h.canRedo());   // the history is gone
        CHECK(h.bytes() == 0);

        // Redo failing rolls back too.
        TestWorld w2;
        UndoHistory h2(w2);
        const uint32_t n = make(w2.scene, 0, "n");
        const Guid ng = guidOf(w2.scene, n);
        h2.begin("Edit", {});
        CHECK(h2.touch(EditTarget::forMaterial(1)));
        w2.materials[0].name = "changed";
        CHECK(h2.touch(EditTarget::forNode(ng)));
        w2.scene.getNode(n).name = "changed";
        h2.commit({});
        CHECK(h2.undo().result == UndoHistory::Result::Done);
        w2.scene.destroyNode(n, orphans);
        const std::string before = dump(w2);
        CHECK(h2.redo().result == UndoHistory::Result::Failed);
        CHECK(dump(w2) == before);
        CHECK(w2.materials[0].name == "mat0");
        CHECK(!h2.canUndo() && !h2.canRedo());
    }

    void testForEachRecord()
    {
        section("undo: record visitor sees both stacks");

        TestWorld w;
        UndoHistory h(w);
        const uint32_t n = make(w.scene, 0, "n");
        w.scene.getNode(n).meshId = 7;
        const Guid g = guidOf(w.scene, n);

        h.begin("Delete", {});
        CHECK(h.destroy(g));
        h.commit({});
        h.begin("Mat", {});
        CHECK(h.touch(EditTarget::forMaterial(1)));
        w.materials[0].name = "x";
        h.commit({});
        CHECK(h.undo().result == UndoHistory::Result::Done);

        size_t destroys = 0, modifies = 0;
        bool sawMesh = false;
        h.forEachRecord([&](const UndoHistory::Record &r) {
            if (r.kind == UndoHistory::Record::Kind::Destroy) {
                ++destroys;
                Node scratch;
                sawMesh |= reflect::readBinary(scratch, r.subtree.nodes.front().data) && scratch.meshId == 7;
            }
            if (r.kind == UndoHistory::Record::Kind::Modify) ++modifies;
        });
        CHECK(destroys == 1 && modifies == 1 && sawMesh);
    }

    // ------------------------------------------------------------------------
    // The one that matters: random histories.
    // ------------------------------------------------------------------------

    void testRandomHistories()
    {
        section("undo: random histories replay exactly");

        std::mt19937 rng(20260916);
        const auto pick = [&](size_t n) { return static_cast<size_t>(rng() % n); };

        int  runs            = 0;
        bool timelineMatches = true;
        bool alwaysIntact    = true;
        bool noOpsInvisible  = true;
        bool fullReplay      = true;
        size_t transactions  = 0;
        size_t undos         = 0;

        for (int run = 0; run < 150; ++run, ++runs) {
            TestWorld w(48);
            UndoHistory h(w);
            Scene &s = w.scene;

            // Starting scene, not recorded.
            for (int i = 0; i < 6; ++i) {
                const std::vector<uint32_t> roots = s.rootNodes();
                const uint32_t parent = (i > 0 && (rng() & 1)) ? roots[pick(roots.size())] : 0;
                s.createNode(parent, "init" + std::to_string(i));
            }

            const auto liveNodes = [&]() {
                std::vector<uint32_t> ids;
                for (uint32_t id = 1; id <= s.nodes().size(); ++id) {
                    if (s.isAlive(id)) ids.push_back(id);
                }
                return ids;
            };

            std::vector<std::string> timeline{ dump(w) };
            size_t pos = 0;

            for (int step = 0; step < 120; ++step) {
                const auto action = static_cast<unsigned>(rng() % 10);

                if (action < 2) {
                    if (h.canUndo()) {
                        timelineMatches &= h.undo().result == UndoHistory::Result::Done;
                        --pos;
                        timelineMatches &= dump(w) == timeline[pos];
                        ++undos;
                    }
                } else if (action < 3) {
                    if (h.canRedo()) {
                        timelineMatches &= h.redo().result == UndoHistory::Result::Done;
                        ++pos;
                        timelineMatches &= dump(w) == timeline[pos];
                    }
                } else {
                    const size_t countBefore = h.undoCount();
                    const bool   redoBefore  = h.canRedo();
                    std::unordered_set<Guid> createdHere;

                    h.begin("random", {});
                    const int ops = 1 + static_cast<int>(rng() % 4);
                    for (int op = 0; op < ops; ++op) {
                        const std::vector<uint32_t> live = liveNodes();
                        const auto kind = static_cast<unsigned>(rng() % 7);

                        if (kind == 0 && live.size() < 40) {
                            // Create under a node that was not itself
                            // created in this transaction, or at the root.
                            uint32_t parent = 0;
                            if (!live.empty() && (rng() & 1)) {
                                const uint32_t candidate = live[pick(live.size())];
                                if (!createdHere.contains(guidOf(s, candidate))) parent = candidate;
                            }
                            const uint32_t id = s.createNode(parent, "n" + std::to_string(rng() % 100));
                            s.getNode(id).setScale(glm::vec3(static_cast<float>(rng() % 5 + 1)));
                            s.getNode(id).meshId = rng() % 4;
                            h.created(guidOf(s, id));
                            createdHere.insert(guidOf(s, id));
                        } else if (kind <= 2 && !live.empty()) {
                            const uint32_t id = live[pick(live.size())];
                            if (h.touch(EditTarget::forNode(guidOf(s, id)))) {
                                Node &node = s.getNode(id);
                                switch (rng() % 3) {
                                case 0: node.setTranslation({ float(rng() % 7), 0, float(rng() % 3) }); break;
                                case 1: node.name = "m" + std::to_string(rng() % 10); break;
                                case 2: node.lightIntensity = float(rng() % 4); break;
                                }
                            }
                        } else if (kind == 3) {
                            const uint32_t m = 1 + static_cast<uint32_t>(pick(w.materials.size()));
                            if (h.touch(EditTarget::forMaterial(m))) {
                                w.materials[m - 1].roughnessFactor = float(rng() % 3) * 0.5f;
                            }
                        } else if (kind == 4 && !live.empty()) {
                            (void)h.destroy(guidOf(s, live[pick(live.size())]));
                        } else if (kind == 5 && live.size() >= 2) {
                            // Raw move to a random valid-or-not placement.
                            const uint32_t id = live[pick(live.size())];
                            const uint32_t parent = (rng() & 1) ? live[pick(live.size())] : 0;
                            const uint32_t first = parent ? s.getNode(parent).firstChildId : s.rootNodeId();
                            std::vector<uint32_t> siblings;
                            for (uint32_t c = first; c != 0; c = s.getNode(c).nextSiblingId) siblings.push_back(c);
                            NodePlacement to;
                            to.parent = parent ? guidOf(s, parent) : Guid{};
                            if (!siblings.empty() && (rng() & 1)) {
                                to.previous = guidOf(s, siblings[pick(siblings.size())]);
                            }
                            const NodePlacement from = s.placementOf(id);
                            if (s.moveNode(id, to)) {
                                h.moved(guidOf(s, id), from, to);
                            }
                        } else if (kind == 6 && live.size() >= 2) {
                            // Editor-style reparent: touch + compensating relink.
                            const uint32_t id = live[pick(live.size())];
                            const uint32_t parent = (rng() & 1) ? live[pick(live.size())] : 0;
                            const Guid g = guidOf(s, id);
                            const NodePlacement from = s.placementOf(id);
                            if (h.touch(EditTarget::forNode(g))) {
                                s.nodes().updateTransforms(s.rootNodeId());
                                if (s.reparentNode(id, parent)) {
                                    h.moved(g, from, s.placementOf(id));
                                }
                            }
                        }
                        alwaysIntact &= integrity(s);
                    }
                    h.commit({});
                    ++transactions;

                    if (h.undoCount() == countBefore + 1) {
                        timeline.resize(pos + 1);
                        timeline.push_back(dump(w));
                        ++pos;
                        noOpsInvisible &= !h.canRedo();
                    } else {
                        // Dropped as a no-op: the world must really be unchanged,
                        // and redo must have survived.
                        noOpsInvisible &= h.undoCount() == countBefore;
                        noOpsInvisible &= dump(w) == timeline[pos];
                        noOpsInvisible &= h.canRedo() == redoBefore;
                    }
                }
                alwaysIntact &= integrity(s);
            }

            // Undo everything, checking each state; then redo everything.
            while (h.canUndo()) {
                fullReplay &= h.undo().result == UndoHistory::Result::Done;
                --pos;
                fullReplay &= dump(w) == timeline[pos];
                alwaysIntact &= integrity(s);
            }
            fullReplay &= pos == 0;
            while (h.canRedo()) {
                fullReplay &= h.redo().result == UndoHistory::Result::Done;
                ++pos;
                fullReplay &= dump(w) == timeline[pos];
                alwaysIntact &= integrity(s);
            }
            fullReplay &= pos + 1 == timeline.size();
        }

        std::printf("  %d runs, %zu transactions, %zu interleaved undos\n", runs, transactions, undos);
        CHECK(timelineMatches);
        CHECK(alwaysIntact);
        CHECK(noOpsInvisible);
        CHECK(fullReplay);
    }
}

void runUndoTests()
{
    testSceneStructureOps();
    testModify();
    testNoOpKeepsRedo();
    testCreateDestroy();
    testReparent();
    testMixedTransactions();
    testNesting();
    testSavePoint();
    testBudget();
    testFailureRollsBack();
    testForEachRecord();
    testRandomHistories();
}
