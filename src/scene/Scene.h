#pragma once
#include <vector>
#include <cstdint>
#include <glm/mat4x4.hpp>
#include "Geometry/NodeWorld.h"

class GeometryStore;
struct SubMesh;


struct DrawItem
{
    const SubMesh *subMesh = nullptr;
    glm::mat4      worldMatrix{ 1.0f };

    // Submesh AABB transformed into world space. Picking rejects against this
    // before touching glm::inverse; frustum uses it as-is.
    glm::vec3      worldBoundsMin{ 0.0f };
    glm::vec3      worldBoundsMax{ 0.0f };

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

    // Rebuilt only when something actually changed. Safe to call twice a frame.
    //
    // DrawItems hold pointers into
    // Mesh::subMeshes, and a mesh that gets unloaded takes those pointers with
    // it. Comparing GeometryStore::revision() means an unload cannot leave a
    // dangling pointer in this cache just because I forgot to call
    // invalidateDrawItems().
    const std::vector<DrawItem> &drawItems(const GeometryStore &geometry);
    void invalidateDrawItems() { m_drawItemsDirty = true; }


private:
    NodeWorld m_nodeWorld;
    uint32_t  m_rootNodeId     = 0;
    uint32_t  m_lastRootNodeId = 0;
    std::vector<DrawItem> m_drawItems;
    bool     m_drawItemsDirty   = true;
    uint64_t m_geometryRevision = 0;
};