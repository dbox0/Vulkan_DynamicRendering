#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class Scene;
class GeometryStore;
class ResourceStore;
class TextureCache;
class VulkanContext;

// ============================================================================
// Scene file (.scene) — save and load.
//
//   SCENE FORMAT:
//   Plain JSON, ordered keys, UTF-8. Nodes are a flat list keyed by Guid so
//   files diff and merge cleanly in git even after reparents.
//
//   {
//     "version": 1,
//     "nodes": [
//       {
//         "id": "0123456789abcdef",
//         "parent": null,
//         "previous": null,          <- sibling before this one; null = first
//         "data": { ...Node fields... },
//         "mesh": {                  <- absent when the node has no mesh
//           "path": "models/kart.gltf",   <- a mesh from a glTF file ...
//           "index": 2,
//           "primitive": "Cube",          <- ... OR a procedural one
//           "instance": 0,                <- same number = same runtime mesh
//           "materials": [ "materials/red.mat", null ]
//         }
//       }
//     ]
//   }
//
//   "data" is  what JsonArchive writes for a Node (name, T/R/S, light
//   settings). Asset references are separate from reflected data because they
//   involve loading GPU resources
//
//   Nodes are written in depth-first pre-order, siblings in order. A reader
//   can therefore reconstruct the hierarchy by appending each node under its
//   named parent without needing to look ahead.
//
// WHAT IS AND IS NOT IN A SCENE FILE
//   In:    node identity, name, transform, light settings, hierarchy, mesh
//          asset references, material assignments per submesh.
//
//   "instance" preserves mesh SHARING: a duplicated node shares its mesh
//   handle (and therefore its material assignments), and so does every node
//   that points at the same entry after a load. Two separate imports of one
//   glTF are two instances and load as two copies.
//
//   A material entry is a .mat path, or null. null means "whatever the mesh
//   comes with": the file's own material for a glTF mesh, the default
//   material for a primitive. Materials that exist only in memory or only
//   inside a glTF cannot be referenced and are saved as null (with a
//   warning when that loses an assignment).
//   Not:   material content (each .mat is its own file, already), vertex/
//          index data (in the glTF), runtime handles.
//
// TOLERANCE
//   * Unknown keys: ignored with a warning.
//   * Missing node fields: kept at default.
//   * A Guid already in use (duplicate in the file): skipped with a warning.
//   * A mesh asset that cannot be loaded: the node is created without a mesh.
//   * A material ID that does not exist: submesh keeps its default (0).
//   * All warnings are returned to the caller, not reported to stderr.
//
// VERSION
//   "version" is there for the first file. It costs one line now and saves
//   editing every .scene file the first time a field is renamed.
// ============================================================================

struct SceneLoadResult
{
    bool                     ok       = false;
    std::vector<std::string> warnings;

    // .mat files the load had to read from disk. The caller marks them as
    // saved in its own bookkeeping (EditorWorld).
    std::vector<uint32_t>    loadedMaterials;
};

struct SceneSaveResult
{
    bool                     ok       = false;
    std::vector<std::string> warnings;   // things that could not be referenced
};

// Writes `file` atomically (temp file + rename): a failed save never leaves a
// half-written scene behind.
SceneSaveResult saveScene(const Scene &scene, const GeometryStore &geometry,
                          const ResourceStore &resources, const TextureCache &cache,
                          const std::filesystem::path &file);

// Parses `file` first; only if it is a valid scene is `clearExisting` called
// and the content built. A bad file therefore leaves the current scene alone.
//
// Opens and submits its own GPU uploads (glTF images, .mat textures). The
// caller still flushes geometry and commits texture descriptors afterwards.
SceneLoadResult loadScene(Scene &scene, GeometryStore &geometry, ResourceStore &resources,
                          VulkanContext &ctx, TextureCache &cache,
                          const std::filesystem::path &file,
                          const std::function<void()> &clearExisting);
