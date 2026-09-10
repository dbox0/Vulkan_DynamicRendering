#pragma once
#include <vector>
#include <cstdint>
#include <glm/mat4x4.hpp>
#include "Geometry/NodeWorld.h"

class GeometryStore;
struct SubMesh;


struct DrawItem
{
    const SubMesh *subMesh    = nullptr;
    glm::mat4      worldMatrix{1.0f};
};


class Scene
{
public:
    void initialize(size_t maxNodes);

    NodeWorld &nodes()             { return m_nodeWorld; }
    Node &getNode(uint32_t nodeId) { return m_nodeWorld.getNode(nodeId); }
    uint32_t rootNodeId() const    { return m_rootNodeId; }
    size_t maxNodes() const        { return m_nodeWorld.maxNodes(); }

    void addRootNode(uint32_t nodeId);
    
    void collectDrawItems(const GeometryStore &geometry, std::vector<DrawItem> &out);

private:
    NodeWorld m_nodeWorld;
    uint32_t  m_rootNodeId     = 0;
    uint32_t  m_lastRootNodeId = 0;

    // Kept as a member so traversal doesn't reallocate every frame.
    std::vector<std::pair<Node *, glm::mat4>> m_traversalStack;
};