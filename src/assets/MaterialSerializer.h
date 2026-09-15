#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <cstdint>
#include <filesystem>

class VulkanContext;
class ResourceStore;
class TextureCache;

// .mat read/write. Free functions rather than ResourceStore methods, for the
// same reason GltfLoader is its own class: ResourceStore knows about GPU
// resources and nothing about the filesystem, and that is worth keeping.

// FORMAT (version 1)
//   Plain JSON. Texture slots are { path, srgb, wrap }, path relative to the
//   asset root with forward slashes. Factors are glTF-convention linear.
//
//   {
//     "version": 1,
//     "name": "Brushed Metal",
//     "baseColorFactor": [0.8, 0.8, 0.82, 1.0],
//     "metallicFactor": 1.0,
//     "roughnessFactor": 0.35,
//     "emissiveFactor": [0.0, 0.0, 0.0],
//     "emissiveStrength": 1.0,
//     "normalScale": 1.0,
//     "occlusionStrength": 1.0,
//     "alphaMode": "Opaque",
//     "alphaCutoff": 0.5,
//     "doubleSided": false,
//     "textures": {
//       "baseColor": { "path": "textures/metal_basecolor.png", "srgb": true },
//       "normal":    { "path": "textures/metal_normal.png", "srgb": false, "wrap": "clamp" }
//     }
//   }

// "version" is there from the first file written. It costs one line now and
// saves hand-editing every .mat on disk the first time a field changes.

// A slot that is absent means "no map" -- it resolves to the white default
// a slot whose file fails to load gets the error
// texture -> "missing" and "broken" have to look different.

// Overwrites `file`. Returns false and reports on IO failure, or when a
// texture on the material has no known source path -- see registerImage() in
// TextureCache for why that happens and how to stop it.

bool saveMaterial(const ResourceStore &resources, const TextureCache &cache,
                  uint32_t materialId, const std::filesystem::path &file);

// Appends a new material to the store and returns its 1-based ID, 0 on
// failure.
//
// `cmd` must come from VulkanContext::beginUpload(); the caller brackets
// begin/submit so several materials share one submission. The caller is also
// responsible for ResourceStore::commitTextureDescriptors() afterwards --
// exactly the contract Application::loadData already follows for glTF.
uint32_t loadMaterial(VulkanContext &ctx, ResourceStore &resources, TextureCache &cache,
                      VkCommandBuffer cmd, const std::filesystem::path &file);