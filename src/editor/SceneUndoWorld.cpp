#include "SceneUndoWorld.h"

#include "../scene/Scene.h"

bool SceneUndoWorld::exists(const EditTarget &target) const
{
    if (target.kind == EditTarget::Kind::Node) {
        return m_scene.findNode(target.node) != 0;
    }
    return existsOther(target);
}

bool SceneUndoWorld::capture(const EditTarget &target, reflect::Blob &out) const
{
    if (target.kind == EditTarget::Kind::Node) {
        return m_scene.captureNode(target.node, out);
    }
    return captureOther(target, out);
}

bool SceneUndoWorld::apply(const EditTarget &target, std::span<const uint8_t> snapshot)
{
    if (target.kind == EditTarget::Kind::Node) {
        return m_scene.applyNode(target.node, snapshot);
    }
    return applyOther(target, snapshot);
}

bool SceneUndoWorld::placement(Guid node, NodePlacement &out) const
{
    const uint32_t id = m_scene.findNode(node);
    if (id == 0) {
        return false;
    }
    out = m_scene.placementOf(id);
    return true;
}

bool SceneUndoWorld::moveNode(Guid node, const NodePlacement &to)
{
    const uint32_t id = m_scene.findNode(node);
    return id != 0 && m_scene.moveNode(id, to);
}

bool SceneUndoWorld::captureSubtree(Guid root, SubtreeSnapshot &out) const
{
    const uint32_t id = m_scene.findNode(root);
    return id != 0 && m_scene.captureSubtree(id, out);
}

bool SceneUndoWorld::restoreSubtree(const SubtreeSnapshot &snapshot)
{
    return m_scene.restoreSubtree(snapshot) != 0;
}

bool SceneUndoWorld::destroySubtree(Guid root)
{
    const uint32_t id = m_scene.findNode(root);
    if (id == 0) {
        return false;
    }
    m_orphans.clear();
    m_scene.destroyNode(id, m_orphans);
    if (!m_orphans.empty()) {
        onMeshesOrphaned(m_orphans);
    }
    return true;
}
