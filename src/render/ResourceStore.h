#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include "../structs.h"
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

    bool initialize();          // descriptor pool/layout/set + purple fallback
    void shutdown();

    // --- images / samplers / textures ------------------------------------
    // Records the upload into commandBuffer; the returned staging buffer must
    // be destroyed by the caller after submit. Returns id 0 on failure.

    uint32_t addImage(VkCommandBuffer commandBuffer, const unsigned char *data,
                      uint32_t width, uint32_t height, int channels,
                      GPUBuffer &outStagingBuffer);
    uint32_t addSampler(const VkSamplerCreateInfo &info);
    uint32_t addTexture(uint32_t imageId, uint32_t samplerId);
    uint32_t addMaterial(const Material &material);
    uint32_t addBuffer(const GPUBuffer &buffer);

    uint32_t fallbackImageId()   const { return m_fallbackImageId; }
    uint32_t fallbackSamplerId() const { return m_fallbackSamplerId; }
    uint32_t fallbackTextureId() const { return m_fallbackTextureId; }

    // Converts a 1-based texture ID to the 0-based descriptor array slot the
    // shader uses. Returns the fallback slot for id 0 or out-of-range.
    uint32_t textureDescriptorSlot(uint32_t textureId) const;

    const GPUBuffer &buffer(uint32_t bufferId) const { return m_buffers[bufferId - 1]; }
    size_t materialCount() const { return m_materials.size(); }
    const std::vector<Material> &materials() const { return m_materials; }

    // --- bindless descriptors --------------------------------------------
    bool updateTextureDescriptors();       // writes all textures into binding 0
    VkDescriptorSet       globalDescriptorSet() const { return m_globalDescSet; }
    VkDescriptorSetLayout globalLayout()        const { return m_globalLayout; }

    // Uploads m_materials into a device buffer; call after all glTF loading.
    bool uploadMaterialBuffer();
    uint64_t materialBufferAddress() const;

private:
    bool createDescriptorSets();
    bool createFallbackTexture();

    VulkanContext &m_ctx;

    std::vector<GPUImage>  m_images;
    std::vector<VkSampler> m_samplers;
    std::vector<Texture>   m_textures;
    std::vector<Material>  m_materials;
    std::vector<GPUBuffer> m_buffers;

    uint32_t m_fallbackImageId   = 0;
    uint32_t m_fallbackSamplerId = 0;
    uint32_t m_fallbackTextureId = 0;
    uint32_t m_materialBufferId  = 0;

    VkDescriptorPool      m_descriptorPool = nullptr;
    VkDescriptorSetLayout m_globalLayout   = nullptr;
    VkDescriptorSet       m_globalDescSet  = nullptr;
};