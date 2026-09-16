#include "Scene.h"
#include <algorithm>
#include <limits>
#include <unordered_set>

#include "../common/Fatal.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <utility>


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

uint32_t Scene::createNode(uint32_t parentId, std::string name, uint32_t meshId, Guid guid)
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
    const uint32_t nodeId = m_nodeWorld.createNode(guid).second;

    {
        Node &node = m_nodeWorld.getNode(nodeId);
        node.name     = std::move(name);
        node.meshId   = meshId;
        node.parentId = parentId;
    }

    appendChild(parentId, nodeId);
    return nodeId;
}

void Scene::appendChild(uint32_t parentId, uint32_t nodeId)
{
    if (parentId == 0) {
        addRootNode(nodeId);
        return;
    }

    m_nodeWorld.getNode(nodeId).parentId = parentId;
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

    m_nodeWorld.flagTopologyChanged();
    invalidateDrawItems();
}

void Scene::linkAfter(uint32_t parentId, uint32_t previousId, uint32_t nodeId)
{
    Node &node = m_nodeWorld.getNode(nodeId);
    node.parentId = parentId;

    if (previousId != 0) {
        Node &previous     = m_nodeWorld.getNode(previousId);
        node.nextSiblingId = previous.nextSiblingId;
        previous.nextSiblingId = nodeId;
        if (parentId == 0 && m_lastRootNodeId == previousId) {
            m_lastRootNodeId = nodeId;
        }
    } else if (parentId == 0) {
        node.nextSiblingId = m_rootNodeId;
        m_rootNodeId       = nodeId;
        if (m_lastRootNodeId == 0) {
            m_lastRootNodeId = nodeId;
        }
    } else {
        Node &parent        = m_nodeWorld.getNode(parentId);
        node.nextSiblingId  = parent.firstChildId;
        parent.firstChildId = nodeId;
    }

    m_nodeWorld.flagTopologyChanged();
    invalidateDrawItems();
}

void Scene::unlink(uint32_t nodeId)
{
    if (!m_nodeWorld.isAlive(nodeId)) {
        return;
    }

    const uint32_t parentId = m_nodeWorld.getNode(nodeId).parentId;
    const uint32_t next     = m_nodeWorld.getNode(nodeId).nextSiblingId;

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

// structure as data

std::vector<uint32_t> Scene::rootNodes() const
{
    std::vector<uint32_t> roots;
    for (uint32_t id = m_rootNodeId; id != 0; id = m_nodeWorld.getNode(id).nextSiblingId) {
        roots.push_back(id);
    }
    return roots;
}

NodePlacement Scene::placementOf(uint32_t nodeId) const
{
    if (!m_nodeWorld.isAlive(nodeId)) {
        fatalError("Scene::placementOf: node is not alive");
    }

    const uint32_t parentId = m_nodeWorld.getNode(nodeId).parentId;
    uint32_t       first    = parentId ? m_nodeWorld.getNode(parentId).firstChildId : m_rootNodeId;

    NodePlacement placement;
    placement.parent = parentId ? m_nodeWorld.getNode(parentId).guid() : Guid{};

    uint32_t previous = 0;
    for (uint32_t id = first; id != 0 && id != nodeId; id = m_nodeWorld.getNode(id).nextSiblingId) {
        previous = id;
    }
    placement.previous = previous ? m_nodeWorld.getNode(previous).guid() : Guid{};
    return placement;
}

bool Scene::moveNode(uint32_t nodeId, const NodePlacement &to)
{
    if (!m_nodeWorld.isAlive(nodeId)) {
        return false;
    }

    const uint32_t parentId   = to.parent.isNull() ? 0 : m_nodeWorld.findNode(to.parent);
    const uint32_t previousId = to.previous.isNull() ? 0 : m_nodeWorld.findNode(to.previous);

    if ((!to.parent.isNull() && parentId == 0) || (!to.previous.isNull() && previousId == 0)) {
        return false;
    }
    if (parentId == nodeId || previousId == nodeId) {
        return false;
    }
    if (parentId != 0 && isDescendantOf(parentId, nodeId)) {
        return false;       // would splice the hierarchy into a ring
    }
    if (previousId != 0 && m_nodeWorld.getNode(previousId).parentId != parentId) {
        return false;
    }

    unlink(nodeId);
    linkAfter(parentId, previousId, nodeId);
    return true;
}

bool Scene::captureNode(Guid guid, reflect::Blob &out) const
{
    const uint32_t nodeId = m_nodeWorld.findNode(guid);
    if (nodeId == 0) {
        return false;
    }
    out.clear();
    reflect::writeBinary(m_nodeWorld.getNode(nodeId), out);
    return true;
}

bool Scene::applyNode(Guid guid, std::span<const uint8_t> snapshot)
{
    const uint32_t nodeId = m_nodeWorld.findNode(guid);
    if (nodeId == 0) {
        return false;
    }
    if (!reflect::readBinary(m_nodeWorld.getNode(nodeId), snapshot)) {
        return false;
    }
    // meshId may have changed, which the transform pass cannot see.
    invalidateDrawItems();
    return true;
}

bool Scene::captureSubtree(uint32_t rootId, SubtreeSnapshot &out) const
{
    if (!m_nodeWorld.isAlive(rootId)) {
        return false;
    }

    out.nodes.clear();
    out.placement = placementOf(rootId);

    // Explicit stack, children pushed in reverse so they pop in sibling order.
    std::vector<uint32_t> stack{ rootId };
    std::vector<uint32_t> children;
    while (!stack.empty()) {
        const uint32_t id = stack.back();
        stack.pop_back();

        const Node &node = m_nodeWorld.getNode(id);

        SubtreeSnapshot::Entry &entry = out.nodes.emplace_back();
        entry.guid   = node.guid();
        entry.parent = node.parentId ? m_nodeWorld.getNode(node.parentId).guid() : Guid{};
        reflect::writeBinary(node, entry.data);

        children.clear();
        for (uint32_t child = node.firstChildId; child != 0;
             child = m_nodeWorld.getNode(child).nextSiblingId) {
            children.push_back(child);
        }
        stack.insert(stack.end(), children.rbegin(), children.rend());
    }
    return true;
}

uint32_t Scene::restoreSubtree(const SubtreeSnapshot &snapshot)
{
    // ---- validate everything before touching anything -------------------
    if (snapshot.nodes.empty()) {
        return 0;
    }
    if (m_nodeWorld.liveCount() + snapshot.nodes.size() > m_nodeWorld.maxNodes()) {
        return 0;
    }

    const NodePlacement &at = snapshot.placement;
    const uint32_t parentId   = at.parent.isNull() ? 0 : m_nodeWorld.findNode(at.parent);
    const uint32_t previousId = at.previous.isNull() ? 0 : m_nodeWorld.findNode(at.previous);
    if ((!at.parent.isNull() && parentId == 0) || (!at.previous.isNull() && previousId == 0)) {
        return 0;
    }
    if (previousId != 0 && m_nodeWorld.getNode(previousId).parentId != parentId) {
        return 0;
    }
    if (snapshot.nodes.front().parent != at.parent) {
        return 0;
    }

    std::unordered_set<Guid> seen;
    seen.reserve(snapshot.nodes.size());
    Node scratch;
    for (size_t i = 0; i < snapshot.nodes.size(); ++i) {
        const SubtreeSnapshot::Entry &entry = snapshot.nodes[i];
        if (entry.guid.isNull() || m_nodeWorld.findNode(entry.guid) != 0 ||
            !seen.insert(entry.guid).second) {
            return 0;
        }
        // Every non-root node's parent comes earlier in pre-order.
        if (i > 0 && !seen.contains(entry.parent)) {
            return 0;
        }
        if (!reflect::readBinary(scratch, entry.data)) {
            return 0;
        }
    }

    //  apply
    const auto create = [this](const SubtreeSnapshot::Entry &entry) {
        const uint32_t id = m_nodeWorld.createNode(entry.guid).second;
        if (!reflect::readBinary(m_nodeWorld.getNode(id), entry.data)) {
            fatalError("Scene::restoreSubtree: validated snapshot failed to apply");
        }
        return id;
    };

    const uint32_t rootId = create(snapshot.nodes.front());
    linkAfter(parentId, previousId, rootId);

    // Pre-order plus append-to-tail rebuilds each child list in its
    // original order.
    for (size_t i = 1; i < snapshot.nodes.size(); ++i) {
        const SubtreeSnapshot::Entry &entry = snapshot.nodes[i];
        const uint32_t id = create(entry);
        appendChild(m_nodeWorld.findNode(entry.parent), id);
    }

    invalidateDrawItems();
    return rootId;
}
