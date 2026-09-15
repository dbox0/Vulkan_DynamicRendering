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
// Which map on a material a texture is being assigned to.
//
// The slot -- not the file -- decides colour space. A PNG carries no such
// information, which is why GltfLoader has to infer it from the materials
// referencing an image, and why a drag-and-drop assignment has to carry the
// slot rather than just a path.
enum class TextureSlot : uint8_t
{
    BaseColor,
    MetallicRoughness,
    Normal,
    Occlusion,
    Emissive
};

constexpr bool slotIsSrgb(TextureSlot slot)
{
    return slot == TextureSlot::BaseColor || slot == TextureSlot::Emissive;
}

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
        LoadModel,         // path
        SaveMaterial,      // materialId, path (empty -> MaterialInfo::sourcePath)
        LoadMaterial,      // path
        CreateMaterial,    // path (empty -> in memory only), nodeId + subMesh optional
        CreateDirectory,   // path (Application uniquifies)
        RenameAsset,       // path + name (a file or folder on disk)
        AssignTexture      // materialId, textureSlot, then EITHER textureId (an
                           // already-loaded texture) OR path (a file to load).
                           // Neither set clears the slot.
    };

    // subMesh sentinel: retarget every submesh of the node's mesh.
    static constexpr uint32_t kAllSubMeshes = 0xFFFFFFFFu;

    Kind          kind      = Kind::CreateEmpty;
    PrimitiveType primitive = PrimitiveType::Cube;

    uint32_t nodeId     = 0;
    uint32_t parentId   = 0;
    uint32_t subMesh    = kAllSubMeshes;
    uint32_t materialId = 0;

    TextureSlot textureSlot = TextureSlot::BaseColor;
    uint32_t    textureId   = 0;

    // RenameAsset: the new name, without a directory. Extension optional --
    // Application keeps the original one when it is left off, so renaming
    // "rock.mat" to "stone" does not produce an extensionless file.
    std::string name;

    // LoadModel, LoadMaterial, SaveMaterial and RenameAsset use this. A path per command is a few dozen bytes on a
    // vector that holds a handful of entries for one frame -- not worth a
    // variant to avoid.
    std::filesystem::path path;
};
