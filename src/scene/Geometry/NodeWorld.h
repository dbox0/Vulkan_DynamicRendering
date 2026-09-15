#pragma once
#include "Node.h"
#include <cassert>
#include <cstdint>
#include <vector>

// Flat node storage with 1-based IDs (id == index + 1, 0 == none).
//
// DELETION IS SOFT. Erasing from the vector would renumber every node behind
// the hole, and the IDs are held by the scene's sibling/child links, by the
// editor's selection and by cached DrawItems. A dead node keeps its slot, is
// unlinked from the hierarchy by Scene, and its ID goes on a free list for
// createNode() to hand out again.

class NodeWorld
{
    std::vector<Node>      m_nodes;
    std::vector<glm::mat4> m_world;       // parallel to m_nodes, world space
    std::vector<uint8_t>   m_worldDirty;  // did this node's world change this pass?
    std::vector<uint8_t>   m_alive;

    std::vector<uint32_t> m_freeSlots;
    std::vector<uint32_t> m_stack;        // DFS scratch, kept to avoid per-frame allocation

    size_t m_maxNodes  = 0;
    size_t m_liveNodes = 0;

    // Set whenever a node is created, destroyed or reparented. Forces one full
    // rebuild, which is what keeps a node whose local matrix arrived via
    // setTransform() (m_changed cleared by the same pass) from being skipped
    // before it ever had a world matrix.
    bool m_topologyDirty = true;

public:
    void initialize(const size_t maxNodes)
    {
        m_maxNodes = maxNodes;
        m_nodes.reserve(m_maxNodes);
        m_world.reserve(m_maxNodes);
        m_worldDirty.reserve(m_maxNodes);
        m_alive.reserve(m_maxNodes);
        m_stack.reserve(64);
    }

    [[nodiscard]] size_t maxNodes() const  { return m_maxNodes; }
    [[nodiscard]] size_t size() const      { return m_nodes.size(); }   // slots, live or not
    [[nodiscard]] size_t liveCount() const { return m_liveNodes; }

    [[nodiscard]] bool isAlive(uint32_t nodeId) const
    {
        return nodeId != 0 && nodeId <= m_alive.size() && m_alive[nodeId - 1] != 0;
    }

    [[nodiscard]] const glm::mat4 &worldMatrix(uint32_t nodeId) const
    {
        assert(nodeId > 0 && nodeId <= m_world.size());
        return m_world[nodeId - 1];
    }

    std::pair<Node &, uint32_t> createNode()
    {
        m_topologyDirty = true;
        ++m_liveNodes;

        if (!m_freeSlots.empty()) {
            const uint32_t nodeId = m_freeSlots.back();
            m_freeSlots.pop_back();

            m_nodes[nodeId - 1] = Node{};          // fresh, and dirty by default
            m_alive[nodeId - 1] = 1;
            return { m_nodes[nodeId - 1], nodeId };
        }

        assert(m_nodes.size() < m_maxNodes && "Node world is at capacity");
        m_nodes.push_back(Node{});
        m_world.push_back(glm::mat4(1.0f));
        m_worldDirty.push_back(1);
        m_alive.push_back(1);

        const auto nodeId = static_cast<uint32_t>(m_nodes.size());
        return { m_nodes[nodeId - 1], nodeId };
    }

    // Marks the slot reusable. Unlinking from the parent's child list and from
    // the scene's root chain is Scene's job -- this class does not know which
    // nodes are roots.
    void markDead(uint32_t nodeId)
    {
        if (!isAlive(nodeId)) {
            return;
        }
        m_alive[nodeId - 1] = 0;
        m_nodes[nodeId - 1] = Node{};   // drops the name and the child links
        m_freeSlots.push_back(nodeId);

        --m_liveNodes;
        m_topologyDirty = true;
    }

    // Call after any reparent that this class did not perform itself.
    void flagTopologyChanged() { m_topologyDirty = true; }

    Node &getNode(uint32_t nodeId)
    {
        assert(nodeId > 0 && "Tried retrieving node with nil ID");
        return m_nodes[nodeId - 1];
    }

    [[nodiscard]] const Node &getNode(uint32_t nodeId) const
    {
        assert(nodeId > 0 && "Tried retrieving node with nil ID");
        return m_nodes[nodeId - 1];
    }

    // Depth first from the root chain. Returns true if any world matrix moved.
    //
    // A child is only pushed once its parent has been popped and written, so
    // m_worldDirty[parent] is always up to date by the time the child reads it
    // -- no ordering assumption about IDs anywhere.
    bool updateTransforms(uint32_t firstRootId)
    {
        const size_t count = m_nodes.size();
        if (m_world.size() != count) {
            m_world.resize(count, glm::mat4(1.0f));
        }
        m_worldDirty.assign(count, 0);

        bool anyMoved = false;

        m_stack.clear();
        for (uint32_t id = firstRootId; id != 0; id = m_nodes[id - 1].nextSiblingId) {
            m_stack.push_back(id);
        }

        while (!m_stack.empty()) {
            const uint32_t nodeId = m_stack.back();
            m_stack.pop_back();

            if (!isAlive(nodeId)) {
                continue;               // a dead node takes its subtree with it
            }

            Node          &node   = m_nodes[nodeId - 1];
            const uint32_t parent = node.parentId;

            uint8_t dirty = (m_topologyDirty || node.hasChanged()) ? 1 : 0;
            if (parent) {
                dirty |= m_worldDirty[parent - 1];
            }

            if (dirty) {
                const glm::mat4 &local = node.getTransform();
                m_world[nodeId - 1]    = parent ? m_world[parent - 1] * local : local;
                m_worldDirty[nodeId - 1] = 1;
                anyMoved = true;
            }
            node.clearChanged();

            for (uint32_t child = node.firstChildId; child != 0;
                 child = m_nodes[child - 1].nextSiblingId) {
                m_stack.push_back(child);
            }
        }

        m_topologyDirty = false;
        return anyMoved;
    }
};
