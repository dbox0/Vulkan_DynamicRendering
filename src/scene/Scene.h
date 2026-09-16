#pragma once
#include <vector>
#include <string>
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
    const NodeWorld &nodes() const { return m_nodeWorld; }
    Node &getNode(uint32_t nodeId) { return m_nodeWorld.getNode(nodeId); }
    uint32_t rootNodeId() const    { return m_rootNodeId; }
    size_t maxNodes() const        { return m_nodeWorld.maxNodes(); }

    bool isAlive(uint32_t nodeId) const { return m_nodeWorld.isAlive(nodeId); }

    // Current slot of a node, 0 if no live node has this Guid. Anything that
    // remembers a node past the current frame stores the Guid and comes
    // through here.
    uint32_t findNode(Guid guid) const { return m_nodeWorld.findNode(guid); }

    void addRootNode(uint32_t nodeId);

    // --- editing ---------------------------------------------------------

    // Creates a node and links it under parentId, or into the root chain when
    // parentId is 0. Returns 0 if the node budget is exhausted.
    //
    // `guid` null (the default) means a new identity. Pass one only to bring
    // back a node that existed before (undo, scene load); see
    // NodeWorld::createNode for the rules.
    uint32_t createNode(uint32_t parentId, std::string name, uint32_t meshId = 0,
                        Guid guid = {});

    // Unlinks nodeId and kills its whole subtree.
    //
    // Does NOT unload meshes -- the scene has no handle on GeometryStore, and
    // several nodes can share one mesh. Instead it appends, to
    // orphanedMeshesOut, every mesh handle that no live node references any
    // more. Those are the ones the caller can safely removeMesh().
    //
    // The refcount is taken AFTER the subtree dies, which is the only order
    // that gives the right answer: asking first would always count the nodes
    // about to be deleted.
    void destroyNode(uint32_t nodeId, std::vector<uint32_t> &orphanedMeshesOut);

    // Moves nodeId under newParentId (0 = root), preserving its world
    // transform. Refuses cycles, which is the one way a hierarchy drag can
    // hang the DFS.
    bool reparentNode(uint32_t nodeId, uint32_t newParentId);

    // How many live nodes reference this mesh handle. The delete path uses it
    // to decide whether a mesh has become garbage.
    size_t meshUsers(uint32_t meshId) const;

    // First alive node carrying a directional light, 0 for none. First rather
    // than "the" one: nothing stops a user making two, and silently using the
    // first makes sense
    uint32_t firstDirectionalLight() const;

    void collectDrawItems(const GeometryStore &geometry, std::vector<DrawItem> &out);

    // Rebuilt only when something actually changed. Safe to call twice a frame.
    //
    // DrawItems hold pointers into Mesh::subMeshes, and a mesh that gets
    // unloaded takes those pointers with it. Comparing GeometryStore::revision()
    // means an unload cannot leave a dangling pointer in this cache just
    // because I forgot to call invalidateDrawItems().
    const std::vector<DrawItem> &drawItems(const GeometryStore &geometry);
    void invalidateDrawItems() { m_drawItemsDirty = true; }

private:
    // Detaches nodeId from whichever chain holds it (parent's child list, or
    // the root list). Leaves nodeId's own parentId/nextSiblingId untouched --
    // the caller either relinks it or kills it.
    void unlink(uint32_t nodeId);

    bool isDescendantOf(uint32_t nodeId, uint32_t ancestorId) const;

    NodeWorld m_nodeWorld;
    uint32_t  m_rootNodeId     = 0;
    uint32_t  m_lastRootNodeId = 0;
    std::vector<DrawItem> m_drawItems;
    std::vector<uint32_t> m_stack;          // subtree DFS, kept to avoid allocating
    std::vector<uint32_t> m_subtree;        // nodes collected by the walk above
    bool     m_drawItemsDirty   = true;
    uint64_t m_geometryRevision = 0;
};
