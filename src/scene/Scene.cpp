#include "Scene.h"
#include "../render/GeometryStore.h"


static void transformAabb(const glm::mat4 &m, const glm::vec3 &lo, const glm::vec3 &hi,
                          glm::vec3 &outLo, glm::vec3 &outHi)
{
    outLo = glm::vec3( std::numeric_limits<float>::max());
    outHi = glm::vec3(-std::numeric_limits<float>::max());
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 p{ (corner & 1) ? hi.x : lo.x,
                           (corner & 2) ? hi.y : lo.y,
                           (corner & 4) ? hi.z : lo.z };
        const glm::vec3 world = glm::vec3(m * glm::vec4(p, 1.0f));
        outLo = glm::min(outLo, world);
        outHi = glm::max(outHi, world);
    }
}

const std::vector<DrawItem> &Scene::drawItems(const GeometryStore &geometry)
{
    const bool moved = m_nodeWorld.updateTransforms();
    const uint64_t revision = geometry.revision();

    if (moved || m_drawItemsDirty || revision != m_geometryRevision) {
        collectDrawItems(geometry, m_drawItems);
        m_drawItemsDirty   = false;
        m_geometryRevision = revision;
    }
    return m_drawItems;
}

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
    invalidateDrawItems();
}

void Scene::collectDrawItems(const GeometryStore &geometry, std::vector<DrawItem> &out)
{
    out.clear();
    const size_t count = m_nodeWorld.size();

    for (uint32_t nodeId = 1; nodeId <= count; ++nodeId) {
        const Node &node = m_nodeWorld.getNode(nodeId);

        // meshAlive rather than a bare non-zero test: mesh IDs are
        // generation-tagged handles, so a node left pointing at an unloaded
        // mesh is detectable instead of resolving to whatever was loaded into
        // that slot next.
        if (!geometry.meshAlive(node.meshId)) {
            continue;
        }

        const Mesh &mesh = geometry.mesh(node.meshId);
        const glm::mat4 &world = m_nodeWorld.worldMatrix(nodeId);

        for (uint32_t s = 0; s < mesh.subMeshes.size(); ++s) {
            const SubMesh &subMesh = mesh.subMeshes[s];

            DrawItem &item = out.emplace_back();
            item.subMesh      = &subMesh;
            item.worldMatrix  = world;
            item.nodeId       = nodeId;
            item.subMeshIndex = s;

            transformAabb(world, subMesh.boundsMin, subMesh.boundsMax,
                          item.worldBoundsMin, item.worldBoundsMax);
        }
    }
}