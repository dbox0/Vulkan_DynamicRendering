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

    // Which node produced this draw. The renderer ignores both; picking needs
    // them to map a triangle hit back to something the inspector can show.
    uint32_t       nodeId       = 0;
    uint32_t       subMeshIndex = 0;
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

};