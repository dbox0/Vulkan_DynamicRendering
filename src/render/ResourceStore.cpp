#include "ResourceStore.h"

#include <volk.h>
#include <vk_mem_alloc.h>
#include <array>
#include <vector>

#include "VulkanContext.h"
#include "../common/errors.h"
#include "../common/constants.h"

// ============================================================================
// lifetime
// ============================================================================

bool ResourceStore::initialize()
{
    if (!createDescriptorSets()) {
        return false;
    }
    // Before the fallback texture: addMaterial() writes through the mapped
    // pointer, so the buffer has to exist first.
    if (!createMaterialBuffer()) {
        return false;
    }
    if (!createFallbackTexture()) {
        return false;
    }
    // Commit slot 0 immediately so the descriptor array is valid even if no
    // model is ever loaded. An empty scene now renders without validation
    // errors.
    return commitTextureDescriptors();
}

void ResourceStore::shutdown()
{
    if (!m_ctx.device()) {
        return;
    }

    if (m_materialPtr) {
        vmaUnmapMemory(m_ctx.allocator(), m_materialBuffer.allocation);
        m_materialPtr = nullptr;
    }
    m_ctx.destroyBuffer(m_materialBuffer);

    if (m_globalLayout) {
        vkDestroyDescriptorSetLayout(m_ctx.device(), m_globalLayout, nullptr);
        m_globalLayout = nullptr;
    }
    if (m_descriptorPool) {
        // Frees m_globalDescSet implicitly.
        vkDestroyDescriptorPool(m_ctx.device(), m_descriptorPool, nullptr);
        m_descriptorPool = nullptr;
        m_globalDescSet = nullptr;
    }

    for (GPUImage &img : m_images) {
        m_ctx.destroyImage(img);
    }
    m_images.clear();

    for (VkSampler sampler : m_samplers) {
        vkDestroySampler(m_ctx.device(), sampler, nullptr);
    }
    m_samplers.clear();

    for (GPUBuffer &buff : m_buffers) {
        m_ctx.destroyBuffer(buff);
    }
    m_buffers.clear();

    m_textures.clear();
    m_materials.clear();
    m_texturesWritten = 0;
}


// adders -- 1-based IDs, 0 means failure/none

uint32_t ResourceStore::addImage(VkCommandBuffer commandBuffer, const unsigned char *data,
                                 uint32_t width, uint32_t height, int channels,
                                 GPUBuffer &outStagingBuffer)
{
    GPUImage gpuImage;
    if (!m_ctx.createImage2D(commandBuffer, data, width, height, channels, gpuImage, outStagingBuffer)) {
        return 0;
    }
    m_images.push_back(gpuImage);
    return static_cast<uint32_t>(m_images.size());
}

uint32_t ResourceStore::addSampler(const VkSamplerCreateInfo &info)
{
    VkSampler sampler = nullptr;
    if (vkCreateSampler(m_ctx.device(), &info, nullptr, &sampler) != VK_SUCCESS) {
        showError("Unable to create texture sampler");
        return 0;
    }
    m_samplers.push_back(sampler);
    return static_cast<uint32_t>(m_samplers.size());
}

uint32_t ResourceStore::addTexture(uint32_t imageId, uint32_t samplerId)
{
    if (m_textures.size() >= MaxTextures) {
        showError("Exceeded the maximum texture count");
        return m_fallbackTextureId;
    }

    if (imageId == 0 || imageId > m_images.size()) {
        imageId = m_fallbackImageId;
    }
    if (samplerId == 0 || samplerId > m_samplers.size()) {
        samplerId = m_fallbackSamplerId;
    }

    m_textures.push_back(Texture{ .imageId = imageId, .samplerId = samplerId });
    return static_cast<uint32_t>(m_textures.size());
}

bool ResourceStore::createMaterialBuffer()
{
    const size_t bytes = MaxMaterials * sizeof(Material);

    // Host-visible and kept mapped. ~80KB at 4096 materials -- comfortably
    // inside the BAR window, and it means the editor can tweak a material
    // without touching a transfer queue. Move it device-local with a staged
    // copy only if profiling ever says the shader read is the problem.
    m_materialBuffer = m_ctx.createBuffer(
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        bytes, true, VMA_MEMORY_USAGE_AUTO);

    if (!m_materialBuffer.vkBuffer) {
        showError("Error creating the material buffer");
        return false;
    }

    void *mapped = nullptr;
    if (vmaMapMemory(m_ctx.allocator(), m_materialBuffer.allocation, &mapped) != VK_SUCCESS) {
        showError("Unable to map the material buffer");
        m_ctx.destroyBuffer(m_materialBuffer);
        return false;
    }
    m_materialPtr = static_cast<Material *>(mapped);

    m_materials.reserve(MaxMaterials);
    return true;
}


uint32_t ResourceStore::addMaterial(const Material &material)
{
    if (m_materials.size() >= MaxMaterials) {
        showError("Exceeded the maximum material count");
        return 0;
    }
    if (!m_materialPtr) {
        showError("addMaterial before initialize()");
        return 0;
    }

    const size_t index = m_materials.size();
    m_materials.push_back(material);
    m_materialPtr[index] = material;

    // No-op on coherent memory, required if VMA hands back non-coherent.
    vmaFlushAllocation(m_ctx.allocator(), m_materialBuffer.allocation,
                       index * sizeof(Material), sizeof(Material));

    return static_cast<uint32_t>(index + 1);
}

bool ResourceStore::updateMaterial(uint32_t materialId, const Material &material)
{
    if (!materialId || materialId > m_materials.size() || !m_materialPtr) {
        showError("updateMaterial with an invalid material ID");
        return false;
    }

    const size_t index = materialId - 1;
    m_materials[index] = material;
    m_materialPtr[index] = material;

    vmaFlushAllocation(m_ctx.allocator(), m_materialBuffer.allocation,
                       index * sizeof(Material), sizeof(Material));
    return true;
}


uint32_t ResourceStore::addBuffer(const GPUBuffer &buffer)
{
    m_buffers.push_back(buffer);
    return static_cast<uint32_t>(m_buffers.size());
}

uint32_t ResourceStore::textureDescriptorSlot(uint32_t textureId) const
{
    // Descriptor array is 0-based; our IDs are 1-based. This is the ONLY
    // place that conversion is allowed to happen.
    if (textureId == 0 || textureId > m_textures.size()) {
        return m_fallbackTextureId ? m_fallbackTextureId - 1 : 0;
    }
    return textureId - 1;
}

// fallback texture

bool ResourceStore::createFallbackTexture()
{
    // Magenta 1x1. Must be the first image, sampler and texture created, so
    // it always occupies descriptor slot 0.
    uint32_t purplePixelData = 0xFF00FFFF;

    VkCommandBuffer cmd = m_ctx.beginTransient();
    if (!cmd) {
        return false;
    }

    GPUBuffer staging;
    m_fallbackImageId = addImage(cmd, reinterpret_cast<const unsigned char *>(&purplePixelData),
                                 1, 1, 4, staging);
    m_ctx.endTransient(cmd);
    m_ctx.destroyBuffer(staging);

    if (!m_fallbackImageId) {
        showError("Unable to create the fallback image");
        return false;
    }

    VkSamplerCreateInfo samplerInfo
    {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .compareEnable = VK_FALSE
    };

    m_fallbackSamplerId = addSampler(samplerInfo);
    if (!m_fallbackSamplerId) {
        showError("Unable to create the fallback texture sampler");
        return false;
    }

    m_fallbackTextureId = addTexture(m_fallbackImageId, m_fallbackSamplerId);
    return m_fallbackTextureId != 0;
}

// ============================================================================
// bindless descriptors

VkDescriptorImageInfo ResourceStore::describeTexture(const Texture &t) const
{
    const bool imageOk   = t.imageId   > 0 && t.imageId   <= m_images.size();
    const bool samplerOk = t.samplerId > 0 && t.samplerId <= m_samplers.size();

    const GPUImage &image = imageOk
        ? m_images[t.imageId - 1]
        : m_images[m_fallbackImageId - 1];
    const VkSampler sampler = samplerOk
        ? m_samplers[t.samplerId - 1]
        : m_samplers[m_fallbackSamplerId - 1];

    if (!imageOk || !samplerOk || !image.imageView) {
        showError("Texture references an invalid image or sampler; using the fallback");
        return VkDescriptorImageInfo
        {
            .sampler     = m_samplers[m_fallbackSamplerId - 1],
            .imageView   = m_images[m_fallbackImageId - 1].imageView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
    }

    return VkDescriptorImageInfo
    {
        .sampler     = sampler,
        .imageView   = image.imageView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
}

bool ResourceStore::commitTextureDescriptors()
{
    const uint32_t total = static_cast<uint32_t>(m_textures.size());
    if (total <= m_texturesWritten) {
        return true;                       // nothing new since last commit
    }

    const uint32_t first = m_texturesWritten;
    const uint32_t count = total - first;

    std::vector<VkDescriptorImageInfo> imageDescriptors;
    imageDescriptors.reserve(count);
    for (uint32_t i = first; i < total; ++i) {
        imageDescriptors.push_back(describeTexture(m_textures[i]));
    }

    // Writing slots [first, total) is safe with frames in flight: no material
    // in any submitted command buffer references a slot that did not exist
    // when it was recorded, and the binding is PARTIALLY_BOUND, so unwritten
    // slots were never a problem either.
    VkWriteDescriptorSet write
    {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_globalDescSet,
        .dstBinding = 0,
        .dstArrayElement = first,
        .descriptorCount = count,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = imageDescriptors.data()
    };

    vkUpdateDescriptorSets(m_ctx.device(), 1, &write, 0, nullptr);
    m_texturesWritten = total;
    return true;
}

bool ResourceStore::replaceTextureDescriptor(uint32_t textureId)
{
    if (!textureId || textureId > m_textures.size()) {
        showError("replaceTextureDescriptor with an invalid texture ID");
        return false;
    }

    const uint32_t slot = textureId - 1;
    if (slot >= m_texturesWritten) {
        // Not committed yet -- commitTextureDescriptors() will pick it up.
        return true;
    }

    const VkDescriptorImageInfo info = describeTexture(m_textures[slot]);

    VkWriteDescriptorSet write
    {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_globalDescSet,
        .dstBinding = 0,
        .dstArrayElement = slot,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &info
    };

    vkUpdateDescriptorSets(m_ctx.device(), 1, &write, 0, nullptr);
    return true;
}

bool ResourceStore::createDescriptorSets()
{
    std::array<VkDescriptorPoolSize, 1> poolSizes
    {
        VkDescriptorPoolSize
        {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = MaxTextures
        }
    };

    VkDescriptorPoolCreateInfo poolInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        // Lets us rewrite descriptors after they have been bound.
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data()
    };

    if (vkCreateDescriptorPool(m_ctx.device(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        showError("Unable to create descriptor pool");
        return false;
    }

    std::array<VkDescriptorSetLayoutBinding, 1> bindings
    {
        VkDescriptorSetLayoutBinding
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = MaxTextures,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
        }
    };

    std::array<VkDescriptorBindingFlags, 1> flags
    {
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(flags.size()),
        .pBindingFlags = flags.data()
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &flagsInfo,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data()
    };

    if (vkCreateDescriptorSetLayout(m_ctx.device(), &layoutInfo, nullptr, &m_globalLayout) != VK_SUCCESS) {
        showError("Unable to create descriptor set layout");
        return false;
    }

    VkDescriptorSetAllocateInfo descSetAllocInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &m_globalLayout
    };

    if (vkAllocateDescriptorSets(m_ctx.device(), &descSetAllocInfo, &m_globalDescSet) != VK_SUCCESS) {
        showError("Unable to allocate descriptor set");
        return false;
    }
    return true;
}

