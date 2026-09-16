#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <filesystem>
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
// FORMAT
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
//         "mesh": {                  <- absent when meshId == 0
//           "path": "models/kart.gltf",
//           "index": 2
//         }
//       }
//     ]
//   }
//
//   "data" is exactly what JsonArchive writes for a Node (name, T/R/S, light
//   settings). Asset references are separate from reflected data because they
//   involve loading GPU resources, not just writing values.
//
//   Nodes are written in depth-first pre-order, siblings in order. A reader
//   can therefore reconstruct the hierarchy by appending each node under its
//   named parent without needing to look ahead.
//
// WHAT IS AND IS NOT IN A SCENE FILE
//   In:    node identity, name, transform, light settings, hierarchy, mesh
//          asset references, material assignments per submesh.
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
};

// Overwrites `file`. Returns false on IO failure.
bool saveScene(const Scene &scene, const GeometryStore &geometry,
               const ResourceStore &resources, const TextureCache &cache,
               const std::filesystem::path &file);

// Clears the scene and loads. Mesh assets are loaded through GltfLoader,
// so `cmd` must come from VulkanContext::beginUpload(); the caller
// submits and commits descriptors after this returns.
SceneLoadResult loadScene(Scene &scene, GeometryStore &geometry, ResourceStore &resources,
                          VulkanContext &ctx, TextureCache &cache, VkCommandBuffer cmd,
                          const std::filesystem::path &file);
