#include "BloomPass.h"
#include <volk.h>
#include <algorithm>
#include "../core/Swapchain.h"
#include "../core/VulkanContext.h"
#include "../../common/errors.h"
#include "../core/vkbarrier.h"

namespace {
    constexpr uint32_t GroupSize = 8;

    uint32_t groupsFor(uint32_t extent) { return (extent + GroupSize - 1) / GroupSize; }
}

bool BloomPass::createResources() {
    const VkSamplerCreateInfo samplerInfo
    {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = VK_LOD_CLAMP_NONE
    };
    if (vkCreateSampler(m_ctx.device(), &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        showError("Failed to create the bloom sampler");
        return false;
    }
    const VkDescriptorSetLayoutBinding bindings[]
    {
        { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
        { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT }
    };
    const VkDescriptorSetLayoutCreateInfo layoutInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings = bindings
    };
    if (vkCreateDescriptorSetLayout(m_ctx.device(), &layoutInfo, nullptr, &m_setLayout) != VK_SUCCESS) {
        showError("Failed to create the bloom descriptor set layout");
        return false;
    }
    const VkDescriptorPoolSize poolSizes[]
    {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * MaxMips },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          2 * MaxMips }
    };
    const VkDescriptorPoolCreateInfo poolInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 2 * MaxMips,
        .poolSizeCount = 2,
        .pPoolSizes = poolSizes
    };
    if (vkCreateDescriptorPool(m_ctx.device(), &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        showError("Failed to create the bloom descriptor pool");
        return false;
    }

    const VkPushConstantRange pushRange
    {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(BloomConstants)
    };
    const VkPipelineLayoutCreateInfo pipeLayoutInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &m_setLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange
    };
    if (vkCreatePipelineLayout(m_ctx.device(), &pipeLayoutInfo, nullptr, &m_layout) != VK_SUCCESS) {
        showError("Failed to create the bloom pipeline layout");
        return false;
    }
    return true;
}

bool BloomPass::createPipeline(VkShaderModule module, VkPipeline &outPipeline)
{
    const VkComputePipelineCreateInfo info
    {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = module,
            .pName  = "main"
        },
        .layout = m_layout,
    };
    if (vkCreateComputePipelines(m_ctx.device(), nullptr, 1, &info, nullptr, &outPipeline) != VK_SUCCESS) {
        showError("Failed to create a bloom compute pipeline");
        return false;
    }
    return true;
}

bool BloomPass::createPipelines()
{
    return createPipeline(m_downShader, m_downPipeline) &&
           createPipeline(m_upShader,   m_upPipeline);
}

void BloomPass::appendShaderPrograms(std::vector<ShaderProgram> &out)
{
    out.push_back(computeProgram("post/bloom_down.comp", &m_downShader, { &m_downPipeline },
                                 [this] { return createPipeline(m_downShader, m_downPipeline); }));
    out.push_back(computeProgram("post/bloom_up.comp", &m_upShader, { &m_upPipeline },
                                 [this] { return createPipeline(m_upShader, m_upPipeline); }));
}

void BloomPass::computeMipChain(uint32_t width, uint32_t height) {
    m_mipCount = 0;

    uint32_t w = std::max(1u,width/2);
    uint32_t h = std::max(1u,height/2);

    while (m_mipCount < MaxMips) {
        m_mipExtents[m_mipCount++] = VkExtent2D{w,h};
        if (std::min(w,h) <= 16) break;
        w = std::max(1u,w/2);
        h = std::max(1u,h/2);
    }
}

bool BloomPass::createChainImage()
{
    const VkImageCreateInfo imageInfo
    {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = Swapchain::HDRFormat,
        .extent        = { m_mipExtents[0].width, m_mipExtents[0].height, 1 },
        .mipLevels     = m_mipCount,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        // TRANSFER_DST is only for the disabled path's clear.
        .usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    const VmaAllocationCreateInfo alloc{ .usage = VMA_MEMORY_USAGE_AUTO };

    if (vmaCreateImage(m_ctx.allocator(), &imageInfo, &alloc,
                       &m_chain.image, &m_chain.allocation, nullptr) != VK_SUCCESS) {
        showError("Failed to create the bloom chain image");
        return false;
                       }

    // vmaCreateImage fills the two handles and nothing else.
    m_chain.mipLevels = m_mipCount;
    return true;
}
bool BloomPass::createChainViews() {
    // Per level: one storage view and one sampled view.
    for (uint32_t mip = 0; mip < m_mipCount; ++mip) {
        VkImageViewCreateInfo storageInfo
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = m_chain.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format =  Swapchain::HDRFormat,
            .subresourceRange
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = mip,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        if (vkCreateImageView(m_ctx.device(),&storageInfo,nullptr,&m_storageViews[mip]) != VK_SUCCESS) {
            showError("Could not create a bloom chain storage view");
            return false;
        }

        VkImageViewCreateInfo sampleInfo
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = m_chain.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format =  Swapchain::HDRFormat,
            .subresourceRange
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = mip,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        if (vkCreateImageView(m_ctx.device(),&sampleInfo,nullptr,&m_sampleViews[mip]) != VK_SUCCESS) {
            showError("Could not create a bloom chain sample view");
            return false;
        }
    }
    VkImageViewCreateInfo chainFullInfo
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = m_chain.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format =  Swapchain::HDRFormat,
            .subresourceRange
        {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = m_mipCount,
            .baseArrayLayer = 0,
            .layerCount = 1
        },
    };

    if (vkCreateImageView(m_ctx.device(),&chainFullInfo,nullptr,&m_chainFullView) != VK_SUCCESS) {
        showError("Could not create a bloom chain full view");
        return false;
    }
    return true;
}

bool BloomPass::allocateSets() {
    const uint32_t setCount = 2* m_mipCount;

    std::array<VkDescriptorSetLayout, 2 * MaxMips> layouts{};
    layouts.fill(m_setLayout); // already created

    std::array<VkDescriptorSet, 2 * MaxMips> sets{};
    const VkDescriptorSetAllocateInfo allocInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_pool,
        .descriptorSetCount = setCount,
        .pSetLayouts = layouts.data(),
    };
    if (vkAllocateDescriptorSets(m_ctx.device(),&allocInfo,sets.data()) != VK_SUCCESS) {
        showError("Failed to allocate bloom descriptor sets");
        return false;
    }
    for (uint32_t mip = 0; mip < m_mipCount; ++mip) {
        m_downSets[mip] = sets[mip];
        m_upSets[mip]   = sets[m_mipCount + mip];
    }
    return true;
}


bool BloomPass::createTargets(uint32_t width, uint32_t height)
{
    computeMipChain(width, height);

    if (!createChainImage() || !createChainViews() || !allocateSets()) {
        return false;
    }

    // Downsample: level i reads level i-1. Level 0's source is the HDR target,
    // which setSourceView fills in (not known here)
    for (uint32_t mip = 1; mip < m_mipCount; ++mip) {
        writeSet(m_downSets[mip], m_sampleViews[mip - 1], VK_IMAGE_LAYOUT_GENERAL,
         m_storageViews[mip]);
    }

    // Upsample: level i reads level i+1 and adds into itself.
    for (uint32_t mip = 0; mip + 1 < m_mipCount; ++mip) {
        writeSet(m_upSets[mip], m_sampleViews[mip + 1], VK_IMAGE_LAYOUT_GENERAL,
         m_storageViews[mip]);
    }
    return true;
}
void BloomPass::destroyTargets()
{
    const VkDevice device = m_ctx.device();
    if (!device) {
        return;
    }

    for (VkImageView &view : m_storageViews) {
        vkDestroyImageView(device, view, nullptr);
        view = nullptr;
    }
    for (VkImageView &view : m_sampleViews) {
        vkDestroyImageView(device, view, nullptr);
        view = nullptr;
    }
    vkDestroyImageView(device, m_chainFullView, nullptr);
    m_chainFullView = nullptr;

    m_hdrView = nullptr;

    m_ctx.destroyImage(m_chain);

    vkResetDescriptorPool(device, m_pool, 0);

    m_downSets.fill(nullptr);
    m_upSets.fill(nullptr);
    m_mipCount = 0;
}

void BloomPass::setSourceView(VkImageView hdrView)
{
    m_hdrView = hdrView;
    writeSet(m_downSets[0], hdrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
         m_storageViews[0]);
}


void BloomPass::writeSet(VkDescriptorSet set, VkImageView srcView,
              VkImageLayout srcLayout, VkImageView dstView) const
{
    const VkDescriptorImageInfo srcInfo{ m_sampler, srcView, srcLayout };
    const VkDescriptorImageInfo dstInfo{ VK_NULL_HANDLE, dstView, VK_IMAGE_LAYOUT_GENERAL };

    const VkWriteDescriptorSet writes[]
    {
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 0,
          .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .pImageInfo = &srcInfo },
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 1,
          .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          .pImageInfo = &dstInfo }
    };
    vkUpdateDescriptorSets(m_ctx.device(), 2, writes, 0, nullptr);
}

void BloomPass::chainBarrier(VkCommandBuffer cmd, uint32_t baseMip, uint32_t mipCount,
                             bool dstReadsAndWrites) const
{
    const VkAccessFlags2 dstAccess = dstReadsAndWrites
            ? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
              VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

    vkutil::imageBarrier(cmd, {
        .image     = m_chain.image,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcStage  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccess = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .dstStage  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccess = dstAccess,
        .range     = { VK_IMAGE_ASPECT_COLOR_BIT, baseMip, mipCount, 0, 1 },
    });
}
void BloomPass::dispatchMip(VkCommandBuffer cmd, VkPipelineLayout layout,
                            VkDescriptorSet set, const BloomConstants &push,
                            VkExtent2D dstExtent) const
{
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(cmd, groupsFor(dstExtent.width), groupsFor(dstExtent.height), 1);
}

void BloomPass::record(VkCommandBuffer cmd) {

    if (m_mipCount == 0) {

        return;
    }
    if (!m_settings.enabled) {
        recordDisabled(cmd);
        return;
    }

    const VkImageSubresourceRange all { VK_IMAGE_ASPECT_COLOR_BIT, 0, m_mipCount, 0, 1 };

    vkutil::imageBarrier(cmd,
        {
        .image     = m_chain.image,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcStage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstStage  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccess = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .range     = all,
        }
    );
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_downPipeline);
    for (uint32_t dst = 0; dst < m_mipCount; ++dst) {
        BloomConstants push
        {
            .dstSize{ m_mipExtents[dst].width, m_mipExtents[dst].height },
            .threshold = m_settings.threshold,
            .softKnee  = m_settings.softKnee,
            .prefilter = (dst == 0) ? 1u : 0u
        };

        // From the true source extent. dstSize * 2 diverges the moment a
        // dimension is odd, and the bloom then slides as it blurs.
        const VkExtent2D src = (dst == 0) ? VkExtent2D{ m_mipExtents[0].width  * 2,
                                                        m_mipExtents[0].height * 2 }
        : m_mipExtents[dst - 1];
        push.srcTexelSize[0] = 1.0f / float(src.width);
        push.srcTexelSize[1] = 1.0f / float(src.height);

        if (dst > 0) {
            chainBarrier(cmd, dst - 1, 1, false);
        }
        dispatchMip(cmd, m_layout, m_downSets[dst], push, m_mipExtents[dst]);
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_upPipeline);

    for (uint32_t dst = m_mipCount - 1; dst-- > 0; ) {
        const VkExtent2D src = m_mipExtents[dst + 1];

        const BloomConstants push
        {
            .srcTexelSize{ 1.0f / float(src.width), 1.0f / float(src.height) },
            .dstSize{ m_mipExtents[dst].width, m_mipExtents[dst].height },
            .filterRadius = m_settings.filterRadius
        };

        // Covers both levels: the source's write and the destination's.
        chainBarrier(cmd, dst, 2, true);
        dispatchMip(cmd, m_layout, m_upSets[dst], push, m_mipExtents[dst]);
    }

    vkutil::imageBarrier(cmd, {
        .image     = m_chain.image,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcStage  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccess = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .dstStage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .range     = all,
    });
}
void BloomPass::recordDisabled(VkCommandBuffer cmd) const
{
    // Skipping outright would leave the chain UNDEFINED, and the tonemap pass
    // samples it either way -> Black Image


    if (!m_chain.image) {
        return;
    }
    const VkImageSubresourceRange all{ VK_IMAGE_ASPECT_COLOR_BIT, 0, m_mipCount, 0, 1 };
    vkutil::imageBarrier(cmd, {
        .image     = m_chain.image,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcStage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstStage  = VK_PIPELINE_STAGE_2_CLEAR_BIT,
        .dstAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .range     = all,
    });

    const VkClearColorValue black{ .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } };
    vkCmdClearColorImage(cmd, m_chain.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &black, 1, &all);

    vkutil::imageBarrier(cmd, {
        .image     = m_chain.image,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcStage  = VK_PIPELINE_STAGE_2_CLEAR_BIT,
        .srcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .range     = all,
    });

}
void BloomPass::destroy()
{
    const VkDevice device = m_ctx.device();
    if (!device) {
        return;
    }

    destroyTargets();

    for (VkPipeline *pipeline : { &m_downPipeline, &m_upPipeline }) {
        if (*pipeline) {
            vkDestroyPipeline(device, *pipeline, nullptr);
            *pipeline = nullptr;
        }
    }
    for (VkShaderModule *module : { &m_downShader, &m_upShader }) {
        if (*module) {
            vkDestroyShaderModule(device, *module, nullptr);
            *module = nullptr;
        }
    }
    if (m_layout) {
        vkDestroyPipelineLayout(device, m_layout, nullptr);
        m_layout = nullptr;
    }
    if (m_pool) {
        vkDestroyDescriptorPool(device, m_pool, nullptr);
        m_pool = nullptr;
    }
    if (m_setLayout) {
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        m_setLayout = nullptr;
    }
    if (m_sampler) {
        vkDestroySampler(device, m_sampler, nullptr);
        m_sampler = nullptr;
    }
}