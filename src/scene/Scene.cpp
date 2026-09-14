#include "Scene.h"
#include "../render/GeometryStore.h"


void Scene::initialize(size_t maxNodes)
{
    m_nodeWorld.initialize(maxNodes);
}

void Scene::addRootNode(uint32_t nodeId)
{
    if (!nodeId) {
        return;
    }
    if (!m_rootNodeId) {
        m_rootNodeId = nodeId;
    } else {
        m_nodeWorld.getNode(m_lastRootNodeId).nextSiblingId = nodeId;
    }
    m_lastRootNodeId = nodeId;
}

void Scene::collectDrawItems(const GeometryStore &geometry, std::vector<DrawItem> &out)
{
    m_nodeWorld.updateTransforms();

    out.clear();
    const size_t count = m_nodeWorld.size();
    for (uint32_t nodeId = 1; nodeId <= count; ++nodeId) {
        const Node &node = m_nodeWorld.getNode(nodeId);
        if (!node.meshId) {
            continue;
        }
        const Mesh &mesh = geometry.mesh(node.meshId);
        const glm::mat4 &world = m_nodeWorld.worldMatrix(nodeId);
        for (uint32_t s = 0; s < mesh.subMeshes.size(); ++s) {
            out.push_back(DrawItem{ &mesh.subMeshes[s], world, nodeId, s });
        }
    }
}