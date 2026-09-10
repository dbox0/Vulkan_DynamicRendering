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
    if (!createFallbackTexture()) {
        return false;
    }
    return true;
}

void ResourceStore::shutdown()
{
    if (!m_ctx.device()) {
        return;
    }

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

uint32_t ResourceStore::addMaterial(const Material &material)
{
    m_materials.push_back(material);
    return static_cast<uint32_t>(m_materials.size());
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

bool ResourceStore::updateTextureDescriptors()
{
    if (m_textures.empty()) {
        return true;
    }

    std::vector<VkDescriptorImageInfo> imageDescriptors;
    imageDescriptors.reserve(m_textures.size());

    for (const Texture &t : m_textures) {
        // addTexture already substitutes the fallback for invalid IDs, but
        // check again: a null sampler or imageView reaching this write is a
        // validation error

        const bool imageOk   = t.imageId   > 0 && t.imageId   <= m_images.size();
        const bool samplerOk = t.samplerId > 0 && t.samplerId <= m_samplers.size();

        if (!imageOk || !samplerOk) {
            showError("Texture references an invalid image or sampler; using the fallback");
        }

        const GPUImage &image = imageOk
            ? m_images[t.imageId - 1]
            : m_images[m_fallbackImageId - 1];
        const VkSampler sampler = samplerOk
            ? m_samplers[t.samplerId - 1]
            : m_samplers[m_fallbackSamplerId - 1];

        if (!image.imageView) {
            showError("Texture image has a null image view; using the fallback");
            imageDescriptors.push_back(
                {
                    .sampler = m_samplers[m_fallbackSamplerId - 1],
                    .imageView = m_images[m_fallbackImageId - 1].imageView,
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                });
            continue;
        }

        imageDescriptors.push_back(
            {
                .sampler = sampler,
                .imageView = image.imageView,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            });
    }

    VkWriteDescriptorSet writeDescriptorSet
    {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_globalDescSet,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = static_cast<uint32_t>(imageDescriptors.size()),
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = imageDescriptors.data()
    };

    vkUpdateDescriptorSets(m_ctx.device(), 1, &writeDescriptorSet, 0, nullptr);
    return true;
}

// ============================================================================
// material buffer

bool ResourceStore::uploadMaterialBuffer()
{
    if (m_materials.empty()) {
        return true;
    }

    const size_t matDataBytes = m_materials.size() * sizeof(Material);

    GPUBuffer matBuffer = m_ctx.createBuffer(
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        matDataBytes, true, VMA_MEMORY_USAGE_AUTO);

    if (!matBuffer.vkBuffer) {
        showError("Error creating material buffer");
        return false;
    }

    m_ctx.mapCopyBufferData(matBuffer, 0, m_materials.data(), matDataBytes);
    m_materialBufferId = addBuffer(matBuffer);
    return true;
}

uint64_t ResourceStore::materialBufferAddress() const
{
    if (!m_materialBufferId) {
        return 0;
    }
    return m_buffers[m_materialBufferId - 1].deviceAddress;
}