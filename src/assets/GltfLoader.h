#pragma once
#include <filesystem>
#include <vector>
#include <cstdint>

// tiny_gltf_v3.h is included ONLY in GltfLoader.cpp. Forward declarations
// here keep it out of every other translation unit -- application.h currently
// drags it (and shaderc, and SDL) into everything that includes it.
struct tg3_model;

class VulkanContext;
class ResourceStore;
class GeometryStore;
class Scene;
struct Image;

// Translates a parsed glTF document into the stores. Contains no Vulkan calls
// of its own -- it goes through ResourceStore / GeometryStore for everything.
class GltfLoader
{
public:
    GltfLoader(VulkanContext &ctx, ResourceStore &resources,
               GeometryStore &geometry, Scene &scene)
        : m_ctx(ctx), m_resources(resources), m_geometry(geometry), m_scene(scene) {}

    // Loads the file and appends its content to the stores and the scene's
    // root chain. Returns false on parse or IO failure.
    bool load(const std::filesystem::path &filepath);

private:
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

    VulkanContext &m_ctx;
    ResourceStore &m_resources;
    GeometryStore &m_geometry;
    Scene         &m_scene;
};