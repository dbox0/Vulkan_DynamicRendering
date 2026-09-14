#pragma once
#include "Node.h"
#include <vector>


class NodeWorld {

    std::vector<Node> m_nodes;
    std::vector<glm::mat4> m_world;        // parallel to m_nodes, world-space
    std::vector<uint8_t>   m_worldDirty;   // did this node's world change?
    size_t m_maxNodes = 0;

public:
    void initialize(const size_t maxNodes) {
        m_maxNodes = maxNodes;
        m_nodes.reserve(m_maxNodes);
    }
    [[nodiscard]] size_t maxNodes() const {return m_maxNodes;}

    [[nodiscard]] size_t size() const { return m_nodes.size(); }

    [[nodiscard]] const glm::mat4 &worldMatrix(uint32_t nodeId) const {
        assert(nodeId > 0 && nodeId <= m_world.size());
        return m_world[nodeId - 1];
    }

    std::pair<Node &, uint32_t> createNode() {
        assert(m_nodes.size() < m_maxNodes && "Node world is at capacity");
        m_nodes.push_back(Node{});
        uint32_t nodeId = m_nodes.size();
        return {m_nodes[nodeId -1], nodeId};
    }

    Node &getNode(uint32_t nodeId) {
        assert(nodeId > 0 && "Tried retrieving node with nil ID");
        return m_nodes[nodeId -1];
    }

    [[nodiscard]] const Node &getNode(uint32_t nodeId) const {
        assert(nodeId > 0 && "Tried retrieving node with nil ID");
        return m_nodes[nodeId - 1];
    }

    // Returns true if anything moved. Relies on parentId < nodeId
    bool updateTransforms()
    {
        const size_t count = m_nodes.size();
        const size_t known = m_world.size();      // nodes added since last pass
        m_world.resize(count);
        m_worldDirty.assign(count, 0);

        bool anyMoved = false;
        for (size_t i = 0; i < count; ++i) {
            Node &node = m_nodes[i];
            const uint32_t parent = node.parentId;
            assert(parent <= i && "Parent must have a lower ID than its child");

            // Read isDirty() before getTransform(), which clears the flag.
            uint8_t dirty = (i >= known || node.isDirty()) ? 1 : 0;
            if (parent) {
                dirty |= m_worldDirty[parent - 1];
            }
            if (!dirty) {
                continue;
            }

            const glm::mat4 &local = node.getTransform();
            m_world[i] = parent ? m_world[parent - 1] * local : local;
            m_worldDirty[i] = 1;
            anyMoved = true;
        }
        return anyMoved;
    }
};
