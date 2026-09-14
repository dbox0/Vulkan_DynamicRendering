#include "Scene.h"
#include "../render/GeometryStore.h"


void Scene::initialize(size_t maxNodes)
{
    m_nodeWorld.initialize(maxNodes);
    m_traversalStack.reserve(maxNodes);
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
    out.clear();
    m_traversalStack.clear();

    // Seed with the root sibling chain.
    for (uint32_t nodeId = m_rootNodeId; nodeId; ) {
        m_traversalStack.push_back({ nodeId, glm::mat4(1.0f) });
        nodeId = m_nodeWorld.getNode(nodeId).nextSiblingId;
    }

    while (!m_traversalStack.empty()) {
        const auto [nodeId, parentTransform] = m_traversalStack.back();
        m_traversalStack.pop_back();

        Node &node = m_nodeWorld.getNode(nodeId);
        const glm::mat4 matWorld = parentTransform * node.getTransform();

        if (node.meshId) {
            const Mesh &mesh = geometry.mesh(node.meshId);
            const uint32_t subMeshCount = static_cast<uint32_t>(mesh.subMeshes.size());
            for (uint32_t s = 0; s < subMeshCount; ++s) {
                out.push_back(DrawItem{ &mesh.subMeshes[s], matWorld, nodeId, s });
            }
        }

        for (uint32_t childNodeId = node.firstChildId; childNodeId; ) {
            m_traversalStack.push_back({ childNodeId, matWorld });
            childNodeId = m_nodeWorld.getNode(childNodeId).nextSiblingId;
        }
    }
}