#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

#include "EditTarget.h"
#include "../assets/PrimitiveBuilder.h"
#include "../common/Guid.h"
#include "../reflect/BinaryArchive.h"

// Editor intents, recorded during build() and drained by Application after it
// returns.
//
//   * build() walks the hierarchy and the submesh table while the panels are
//     drawing. Creating a mesh, unloading one, or deleting a node mutates the
//     exact containers being iterated.
//   * build() takes GeometryStore by const reference
//   * Creating a primitive owes a flushUploads(). Batching every creation in a
//     frame into one submit falls out of this for free.
//   * It is the single path by which the editor changes anything. Undo can
//     only record what passes through here,
//
// NODES ARE NAMED BY GUID.
//   Commands are applied in order, and an earlier one can recycle a slot: a
//   DeleteNode followed by a CreateEmpty in the same frame hands the dead
//   node's slot to the new one, and a later command naming that slot would
//   hit the wrong node. Application resolves each Guid when it gets to it.
//
// PROPERTY EDITS: BeginEdit, Modify..., EndEdit
//   A panel edits a COPY of an object and submits the copy's snapshot as a
//   Modify. Modifies are grouped into edits, one per user gesture that
//   becomes one undo step. EditRecorder does the grouping
//
//   Invariant: a BeginEdit ... EndEdit run never has another command inside
//   it. An edit may stay open across frames (a drag), but any other command
//   closes it first.

// Which map on a material a texture is being assigned to.
//
// The slot decides colour space. A PNG carries no such
// information, which is why GltfLoader has to infer it from the materials
// referencing an image, and why a drag-and-drop assignment has to carry thes slot (not path)


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
        CreatePrimitive,   // primitive, parent
        CreateEmpty,       // parent
        CreateLight,       // parent
        DeleteNode,        // node
        DuplicateNode,     // node
        AssignMaterial,    // node, subMesh, materialId
        ReparentNode,      // node, parent
        LoadModel,         // path
        SaveMaterial,      // materialId, path (empty -> MaterialInfo::sourcePath)
        LoadMaterial,      // path
        CreateMaterial,    // path (empty -> in memory only), nodeId + subMesh optional
        CreateDirectory,   // path (Application uniquifies)
        RenameAsset,       // path + name (a file or folder on disk)
        AssignTexture,     // materialId, textureSlot, then textureId (an
                           // already-loaded texture) OR path (a file to load).

        BeginEdit,         // editId, name
        Modify,            // editId, target, snapshot (full new state)
        EndEdit,           // editId

        Undo,
        Redo
    };

    // subMesh sentinel: retarget every submesh of the node's mesh.
    static constexpr uint32_t kAllSubMeshes = 0xFFFFFFFFu;

    Kind          kind      = Kind::CreateEmpty;
    PrimitiveType primitive = PrimitiveType::Cube;

    Guid     node;                   // the node acted on
    Guid     parent;                 // null = scene root
    uint32_t subMesh    = kAllSubMeshes;
    uint32_t materialId = 0;

    TextureSlot textureSlot = TextureSlot::BaseColor;
    uint32_t    textureId   = 0;

    // RenameAsset: new name, extension optional --
    // Application keeps  original extension  when left out
    std::string name;
    std::filesystem::path path;

    // BeginEdit / Modify / EndEdit.
    uint32_t      editId = 0;
    EditTarget    target;
    reflect::Blob snapshot;
};
