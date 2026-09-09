#pragma once
#include "Node.h"
#include <vector>


class NodeWorld {

    std::vector<Node> m_nodes;
    size_t m_maxNodes = 0;

public:
    void initialize(const size_t maxNodes) {
        m_maxNodes = maxNodes;
        m_nodes.reserve(m_maxNodes);
    }
    [[nodiscard]] size_t maxNodes() const {return m_maxNodes;}

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
};
