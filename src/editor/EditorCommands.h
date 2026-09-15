#pragma once
#include <cstdint>
#include <filesystem>
#include "../assets/PrimitiveBuilder.h"

// Editor intents, recorded during build() and drained by Application after it
// returns.
//
// WHY A QUEUE RATHER THAN DIRECT CALLS.
//   * build() walks the hierarchy and the submesh table while the panels are
//     drawing. Creating a mesh, unloading one, or deleting a node mutates the
//     exact containers being iterated -- deleting the node whose tree row is
//     open is the obvious one, but "assign material" reaches into a Mesh the
//     inspector is holding a reference to.
//   * build() takes GeometryStore by const reference on purpose, and the way
//     to keep that is to not need write access, rather than to widen it.
//   * Creating a primitive owes a flushUploads(). Batching every creation in a
//     frame into one submit falls out of this for free.
struct EditorCommand
{
    enum class Kind : uint8_t
    {
        CreatePrimitive,   // primitive, parentId
        CreateEmpty,       // parentId
        DeleteNode,        // nodeId
        DuplicateNode,     // nodeId
        AssignMaterial,    // nodeId, subMesh, materialId
        ReparentNode,      // nodeId, parentId
        LoadModel          // path
    };

    // subMesh sentinel: retarget every submesh of the node's mesh.
    static constexpr uint32_t kAllSubMeshes = 0xFFFFFFFFu;

    Kind          kind      = Kind::CreateEmpty;
    PrimitiveType primitive = PrimitiveType::Cube;

    uint32_t nodeId     = 0;
    uint32_t parentId   = 0;
    uint32_t subMesh    = kAllSubMeshes;
    uint32_t materialId = 0;

    // Only LoadModel uses this. A path per command is a few dozen bytes on a
    // vector that holds a handful of entries for one frame -- not worth a
    // variant to avoid.
    std::filesystem::path path;
};
