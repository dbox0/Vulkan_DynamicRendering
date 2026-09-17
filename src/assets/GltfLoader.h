#pragma once
#include <filesystem>
#include <vector>
#include <cstdint>

struct tg3_model;

class VulkanContext;
class ResourceStore;
class GeometryStore;
class Scene;
class TextureCache;
struct Image;

// Translates a parsed glTF document into the stores.
class GltfLoader
{
public:
    // The cache is taken so every image this loader uploads is registered
    // against its source file. Without that, materials imported from a .gltf
    // resolve to images with no known path and cannot be saved as .mat and
    // the same PNG referenced by two files gets uploaded twice.
    GltfLoader(VulkanContext &ctx, ResourceStore &resources,
               GeometryStore &geometry, Scene &scene, TextureCache &cache)
        : m_ctx(ctx), m_resources(resources), m_geometry(geometry), m_scene(scene),
          m_cache(cache) {}

    // Loads the file and appends its content to the stores and the scene's
    // root chain. Returns false on parse or IO failure.
    bool load(const std::filesystem::path &filepath);

    // Loads images, materials and meshes but creates NO scene nodes. Used by
    // the scene loader, which builds the hierarchy from the .scene file and
    // only needs the meshes. meshIdsOut[i] is the handle for the file's mesh
    // i (0 where that mesh failed). The caller owns every returned mesh,
    // including the ones it does not end up using.
    bool loadMeshesOnly(const std::filesystem::path &filepath, std::vector<uint32_t> &meshIdsOut);

private:
    bool loadImpl(const std::filesystem::path &filepath, bool importNodes,
                  std::vector<uint32_t> *meshIdsOut);

    std::vector<Image>    loadImages(const tg3_model &model, const std::filesystem::path &imageDir) const;

    // Decides each image's VkFormat (sRGB vs linear) from the material slots
    // that reference it. Must run between loadImages() and uploadImages().
    void assignImageColorSpaces(const tg3_model &model, std::vector<Image> &images) const;

    std::vector<uint32_t> uploadImages(const std::vector<Image> &images);
    std::vector<uint32_t> loadSamplers(const tg3_model &model);
    std::vector<uint32_t> loadTextures(const tg3_model &model,
                                       const std::vector<uint32_t> &imageIds,
                                       const std::vector<uint32_t> &samplerIds);
    std::vector<uint32_t> loadMaterials(const tg3_model &model,
                                        const std::vector<uint32_t> &textureIds);
    std::vector<uint32_t> loadMeshes(const tg3_model &model,
                                     const std::vector<uint32_t> &materialIds);

    uint32_t importNode(const tg3_model &model, int32_t nodeIndex,
                        uint32_t parentId, uint32_t prevSiblingId,
                        const std::vector<uint32_t> &meshIds);

    std::string   m_sourcePath;   // ASSET_DIR-relative, set in load()
    VulkanContext &m_ctx;
    ResourceStore &m_resources;
    GeometryStore &m_geometry;
    Scene         &m_scene;
    TextureCache  &m_cache;
};