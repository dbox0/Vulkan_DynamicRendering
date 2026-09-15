#include "Scene.h"
#include "../render/GeometryStore.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <utility>


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
    const bool moved = m_nodeWorld.updateTransforms(m_rootNodeId);
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
    m_nodeWorld.getNode(nodeId).parentId = 0;
    invalidateDrawItems();
}

// ---------------------------------------------------------------------------
// editing
// ---------------------------------------------------------------------------

uint32_t Scene::createNode(uint32_t parentId, std::string name, uint32_t meshId)
{
    if (m_nodeWorld.liveCount() >= m_nodeWorld.maxNodes()) {
        return 0;
    }
    if (parentId != 0 && !m_nodeWorld.isAlive(parentId)) {
        parentId = 0;
    }

    // createNode() may reallocate nothing (the vector is reserved to maxNodes)
    // but taking the ID first and re-fetching keeps that an implementation
    // detail rather than a promise this function relies on.
    const uint32_t nodeId = m_nodeWorld.createNode().second;

    {
        Node &node = m_nodeWorld.getNode(nodeId);
        node.name     = std::move(name);
        node.meshId   = meshId;
        node.parentId = parentId;
    }

    if (parentId == 0) {
        addRootNode(nodeId);
        return nodeId;
    }

    // Append to the parent's child list. Walking to the tail keeps new nodes
    // in creation order in the hierarchy panel; prepending would be O(1) but
    // makes the panel reshuffle everytime a user does something
    Node &parent = m_nodeWorld.getNode(parentId);
    if (parent.firstChildId == 0) {
        parent.firstChildId = nodeId;
    } else {
        uint32_t tail = parent.firstChildId;
        while (m_nodeWorld.getNode(tail).nextSiblingId != 0) {
            tail = m_nodeWorld.getNode(tail).nextSiblingId;
        }
        m_nodeWorld.getNode(tail).nextSiblingId = nodeId;
    }

    invalidateDrawItems();
    return nodeId;
}

void Scene::unlink(uint32_t nodeId)
{
    if (!m_nodeWorld.isAlive(nodeId)) {
        return;
    }

    const uint32_t parentId = m_nodeWorld.getNode(nodeId).parentId;
    const uint32_t next     = m_nodeWorld.getNode(nodeId).nextSiblingId;

    // this runs on user actions, not per frame.
    if (parentId == 0) {
        if (m_rootNodeId == nodeId) {
            m_rootNodeId = next;
        } else {
            for (uint32_t id = m_rootNodeId; id != 0; id = m_nodeWorld.getNode(id).nextSiblingId) {
                if (m_nodeWorld.getNode(id).nextSiblingId == nodeId) {
                    m_nodeWorld.getNode(id).nextSiblingId = next;
                    break;
                }
            }
        }

        // The tail cache is only valid while it points at a linked node.
        if (m_lastRootNodeId == nodeId) {
            m_lastRootNodeId = 0;
            for (uint32_t id = m_rootNodeId; id != 0; id = m_nodeWorld.getNode(id).nextSiblingId) {
                m_lastRootNodeId = id;
            }
        }
    } else {
        Node &parent = m_nodeWorld.getNode(parentId);
        if (parent.firstChildId == nodeId) {
            parent.firstChildId = next;
        } else {
            for (uint32_t id = parent.firstChildId; id != 0;
                 id = m_nodeWorld.getNode(id).nextSiblingId) {
                if (m_nodeWorld.getNode(id).nextSiblingId == nodeId) {
                    m_nodeWorld.getNode(id).nextSiblingId = next;
                    break;
                }
            }
        }
    }

    m_nodeWorld.flagTopologyChanged();
    invalidateDrawItems();
}

void Scene::destroyNode(uint32_t nodeId, std::vector<uint32_t> &orphanedMeshesOut)
{
    if (!m_nodeWorld.isAlive(nodeId)) {
        return;
    }

    // Unlink first. markDead() resets the node, links included, so the parent
    // has to stop pointing at nodeId before anything is wiped.
    unlink(nodeId);

    // Collect the whole subtree before killing any of it -- the walk uses the
    // child links that markDead() is about to clear.
    m_subtree.clear();
    m_stack.clear();
    m_stack.push_back(nodeId);

    while (!m_stack.empty()) {
        const uint32_t current = m_stack.back();
        m_stack.pop_back();

        if (!m_nodeWorld.isAlive(current)) {
            continue;
        }
        for (uint32_t child = m_nodeWorld.getNode(current).firstChildId; child != 0;
             child = m_nodeWorld.getNode(child).nextSiblingId) {
            m_stack.push_back(child);
        }
        m_subtree.push_back(current);
    }

    // Mesh handles are read here, while the nodes still have them.
    const size_t firstCandidate = orphanedMeshesOut.size();
    for (const uint32_t id : m_subtree) {
        const uint32_t meshId = m_nodeWorld.getNode(id).meshId;
        if (meshId != 0) {
            orphanedMeshesOut.push_back(meshId);
        }
    }

    for (const uint32_t id : m_subtree) {
        m_nodeWorld.markDead(id);
    }

    // Now that the subtree is gone, anything still referenced by a survivor is
    // not orphaned -> Example: second node instancing the same mesh

    size_t keep = firstCandidate;
    for (size_t i = firstCandidate; i < orphanedMeshesOut.size(); ++i) {
        const uint32_t meshId = orphanedMeshesOut[i];

        bool alreadyListed = false;
        for (size_t j = firstCandidate; j < keep; ++j) {
            alreadyListed |= orphanedMeshesOut[j] == meshId;
        }
        if (!alreadyListed && meshUsers(meshId) == 0) {
            orphanedMeshesOut[keep++] = meshId;
        }
    }
    orphanedMeshesOut.resize(keep);

    invalidateDrawItems();
}

bool Scene::isDescendantOf(uint32_t nodeId, uint32_t ancestorId) const
{
    if (ancestorId == 0) {
        return false;
    }
    for (uint32_t id = nodeId; id != 0; id = m_nodeWorld.getNode(id).parentId) {
        if (id == ancestorId) {
            return true;
        }
    }
    return false;
}

bool Scene::reparentNode(uint32_t nodeId, uint32_t newParentId)
{
    if (!m_nodeWorld.isAlive(nodeId) || nodeId == newParentId) {
        return false;
    }
    if (newParentId != 0 && !m_nodeWorld.isAlive(newParentId)) {
        return false;
    }
    // Dropping a node onto its own descendant would splice the hierarchy into
    // a ring, and the transform DFS would never terminate.
    if (isDescendantOf(newParentId, nodeId)) {
        return false;
    }

    const glm::mat4 world = m_nodeWorld.worldMatrix(nodeId);

    unlink(nodeId);
    m_nodeWorld.getNode(nodeId).nextSiblingId = 0;
    m_nodeWorld.getNode(nodeId).parentId      = newParentId;

    if (newParentId == 0) {
        addRootNode(nodeId);
    } else {
        Node &parent = m_nodeWorld.getNode(newParentId);
        if (parent.firstChildId == 0) {
            parent.firstChildId = nodeId;
        } else {
            uint32_t tail = parent.firstChildId;
            while (m_nodeWorld.getNode(tail).nextSiblingId != 0) {
                tail = m_nodeWorld.getNode(tail).nextSiblingId;
            }
            m_nodeWorld.getNode(tail).nextSiblingId = nodeId;
        }
    }

    // Keep the node where it visually was. The new parent's world matrix is
    // last frame's, which is correct: nothing has moved this frame yet.
    const glm::mat4 parentWorld = newParentId ? m_nodeWorld.worldMatrix(newParentId)
                                              : glm::mat4(1.0f);
    m_nodeWorld.getNode(nodeId).setTransform(glm::inverse(parentWorld) * world);

    m_nodeWorld.flagTopologyChanged();
    invalidateDrawItems();
    return true;
}

size_t Scene::meshUsers(uint32_t meshId) const
{
    if (meshId == 0) {
        return 0;
    }
    size_t users = 0;
    for (uint32_t nodeId = 1; nodeId <= m_nodeWorld.size(); ++nodeId) {
        if (m_nodeWorld.isAlive(nodeId) && m_nodeWorld.getNode(nodeId).meshId == meshId) {
            ++users;
        }
    }
    return users;
}

// ---------------------------------------------------------------------------

uint32_t Scene::firstDirectionalLight() const
{
    const size_t count = m_nodeWorld.size();
    for (uint32_t nodeId = 1; nodeId <= count; ++nodeId) {
        if (m_nodeWorld.isAlive(nodeId) &&
            m_nodeWorld.getNode(nodeId).lightType == LightType::Directional) {
            return nodeId;
        }
    }
    return 0;
}

void Scene::collectDrawItems(const GeometryStore &geometry, std::vector<DrawItem> &out)
{
    out.clear();
    const size_t count = m_nodeWorld.size();

    for (uint32_t nodeId = 1; nodeId <= count; ++nodeId) {
        if (!m_nodeWorld.isAlive(nodeId)) {
            continue;
        }
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
