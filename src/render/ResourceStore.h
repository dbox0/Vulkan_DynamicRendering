#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <../assets/Material.h>
#include "../common/gpu_types.h"
#include <volk.h>

class VulkanContext;

// Owns every bindless GPU resource: images, samplers, textures, materials and
// raw buffers, plus the global descriptor set the fragment shader samples
// through.
//
// ID CONVENTION
//   * Returned IDs are 1-based:  id == vector index + 1.  id 0 == "none".
//   * Texture slot 0 (i.e. texture ID 1) is always the purple fallback
//   * Material::textureIndex is 0-BASED, because the shader indexes the
//     descriptor array directly. Converting is this class's job

class ResourceStore
{
public:
        explicit ResourceStore(VulkanContext &ctx) : m_ctx(ctx) {}
    ResourceStore(const ResourceStore &) = delete;
    ResourceStore &operator=(const ResourceStore &) = delete;

    bool initialize();          // descriptors + material buffer + fallback
    void shutdown();

    // --- images / samplers / textures ------------------------------------
    // Records the upload into commandBuffer; the returned staging buffer must
    // be destroyed by the caller after submit. Returns id 0 on failure.

    uint32_t addImage(VkCommandBuffer commandBuffer, const unsigned char *data,
                      uint32_t width, uint32_t height, int channels,
                      GPUBuffer &outStagingBuffer);
    uint32_t addSampler(const VkSamplerCreateInfo &info);
    uint32_t addTexture(uint32_t imageId, uint32_t samplerId);
    uint32_t addBuffer(const GPUBuffer &buffer);

    uint32_t fallbackImageId()   const { return m_fallbackImageId; }
    uint32_t fallbackSamplerId() const { return m_fallbackSamplerId; }
    uint32_t fallbackTextureId() const { return m_fallbackTextureId; }

    // Converts a 1-based texture ID to the 0-based descriptor array slot the
    // shader uses. Returns the fallback slot for id 0 or out-of-range.
    uint32_t textureDescriptorSlot(uint32_t textureId) const;

    const GPUBuffer &buffer(uint32_t bufferId) const { return m_buffers[bufferId - 1]; }

    // --- materials -------------------------------------------------------
    // Writes through to the mapped material buffer. No commit step.
    uint32_t addMaterial(const Material &material);

    // In-place edit, for the editor's material inspector. Safe at any time:
    // the shader reads whatever is there when the draw executes, and a torn
    // read at worst shows one stale frame.
    bool updateMaterial(uint32_t materialId, const Material &material);

    const Material &material(uint32_t materialId) const { return m_materials[materialId - 1]; }
    size_t materialCount() const { return m_materials.size(); }
    uint64_t materialBufferAddress() const { return m_materialBuffer.deviceAddress; }

    // --- bindless descriptors --------------------------------------------
    // Writes only the texture slots added since the last call. Cheap, and
    // safe to call right after a load without stalling.
    bool commitTextureDescriptors();

    // Rewrites one already-committed slot (swapping an image on a live
    // material). The caller MUST ensure no submitted frame is still reading
    // it -- vkDeviceWaitIdle, or defer this behind MaxFramesInFlight.
    bool replaceTextureDescriptor(uint32_t textureId);

    VkDescriptorSet       globalDescriptorSet() const { return m_globalDescSet; }
    VkDescriptorSetLayout globalLayout()        const { return m_globalLayout; }

private:
    bool createDescriptorSets();
    bool createMaterialBuffer();
    bool createFallbackTexture();


    struct Texture
    {
        uint32_t imageId = 0;
        uint32_t samplerId = 0;
    };

    // Builds the VkDescriptorImageInfo for one texture, substituting the
    // fallback for anything malformed.
    VkDescriptorImageInfo describeTexture(const Texture &t) const;

    VulkanContext &m_ctx;

    std::vector<GPUImage>  m_images;
    std::vector<VkSampler> m_samplers;
    std::vector<Texture>   m_textures;
    std::vector<Material>  m_materials;   // CPU mirror, for the inspector
    std::vector<GPUBuffer> m_buffers;

    uint32_t m_fallbackImageId   = 0;
    uint32_t m_fallbackSamplerId = 0;
    uint32_t m_fallbackTextureId = 0;

    // How many descriptor slots have actually been written. Everything below
    // this index is potentially in use by an in-flight frame.
    uint32_t m_texturesWritten = 0;

    GPUBuffer m_materialBuffer;
    Material *m_materialPtr = nullptr;    // persistent map

    VkDescriptorPool      m_descriptorPool = nullptr;
    VkDescriptorSetLayout m_globalLayout   = nullptr;
    VkDescriptorSet       m_globalDescSet  = nullptr;
};
