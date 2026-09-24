#include "GtaoPass.h"

#include <volk.h>
#include <algorithm>
#include <span>
#include <glm/gtc/matrix_transform.hpp>

#include "../../core/VulkanContext.h"
#include "../../core/vkbarrier.h"
#include "../../../common/errors.h"

namespace {
    constexpr uint32_t GroupSize = 8;
    uint32_t groupsFor(uint32_t extent, uint32_t pixelsPerGroup) { return (extent + pixelsPerGroup - 1) / pixelsPerGroup; }

    constexpr VkImageSubresourceRange colorRange(uint32_t mips)
    {
        return { VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1 };
    }

    vkutil::ImageBarrier writeToRead(VkImage image, uint32_t mips = 1)
    {
        return {
            .image     = image,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcStage  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccess = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .dstStage  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .range     = colorRange(mips),
        };
    }

    vkutil::ImageBarrier discardToGeneral(VkImage image, VkPipelineStageFlags2 lastReader, uint32_t mips = 1)
    {
        return {
            .image     = image,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcStage  = lastReader,
            .dstStage  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccess = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .range     = colorRange(mips),
        };
    }
}

void GtaoPass::appendShaderPrograms(std::vector<ShaderProgram> &out)
{
    static constexpr const char *Files[StageCount]
    {
        "ao/gtao_prefilter.comp", "ao/gtao_main.comp", "ao/gtao_denoise.comp", "ao/gtao_temporal.comp"
    };
    for (uint32_t i = 0; i < StageCount; ++i) {
        const Stage stage = static_cast<Stage>(i);
        out.push_back(computeProgram(Files[i], &m_shaders[i], { &m_pipelines[i] },
                                     [this, stage] { return createPipeline(stage, m_pipelines[stage]); }));
    }
}

//Resources

bool GtaoPass::createResources() {
    const VkSamplerCreateInfo samplerInfo
    {
        .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter    = VK_FILTER_NEAREST,
        .minFilter    = VK_FILTER_NEAREST,
        .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod       = VK_LOD_CLAMP_NONE
    };
    if (vkCreateSampler(m_ctx.device(), &samplerInfo, nullptr, &m_pointSampler) != VK_SUCCESS) {
        showError("Failed to create the GTAO sampler");
        return false;
    }

    constexpr VkDescriptorType   Sampled    = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    constexpr VkDescriptorType   Storage    = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    constexpr VkShaderStageFlags Cs         = VK_SHADER_STAGE_COMPUTE_BIT;

    const std::array<std::vector<VkDescriptorSetLayoutBinding>, StageCount> bindings
    {{
        { { 0, Sampled, 1, Cs, nullptr }, { 1, Storage, DepthMips, Cs, nullptr } },                         // prefilter
        { { 0, Sampled, 1, Cs, nullptr }, { 1, Storage, 1, Cs, nullptr }, { 2, Storage, 1, Cs, nullptr } }, // main
        { { 0, Sampled, 1, Cs, nullptr }, { 1, Sampled, 1, Cs, nullptr }, { 2, Storage, 1, Cs, nullptr } }, // denoise
        { { 0, Sampled, 1, Cs, nullptr }, { 1, Sampled, 1, Cs, nullptr }, { 2, Storage, 1, Cs, nullptr },
          { 3, Sampled, 1, Cs, nullptr } },
    }};

    for (uint32_t stage = 0; stage < StageCount; ++stage) {
        const uint32_t pushSize = stage == Temporal ? sizeof(GtaoTemporalConstants) : sizeof(GtaoConstants);
        const VkPushConstantRange pushRange{ Cs, 0, pushSize };
        const VkDescriptorSetLayoutCreateInfo layoutInfo
        {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = static_cast<uint32_t>(bindings[stage].size()),
            .pBindings    = bindings[stage].data(),
        };
        if (vkCreateDescriptorSetLayout(m_ctx.device(), &layoutInfo, nullptr, &m_setLayouts[stage]) != VK_SUCCESS) {
            showError("Failed to create the descriptor set layout");
            return false;
        }
        const VkPipelineLayoutCreateInfo pipeLayoutInfo
        {
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount         = 1,
            .pSetLayouts            = &m_setLayouts[stage],
            .pushConstantRangeCount = 1,
            .pPushConstantRanges    = &pushRange
        };
        if (vkCreatePipelineLayout(m_ctx.device(), &pipeLayoutInfo, nullptr, &m_layouts[stage]) != VK_SUCCESS) {
            showError("Failed to create GTAO pipeline layout");
            return false;
        }
    }

    const VkDescriptorPoolSize poolSizes[]
    {
        { Sampled, 1 + 1 + DenoiseSetCount * 2 + HistoryCount * 3 },
        { Storage, DepthMips + 2 + DenoiseSetCount + HistoryCount }
    };
    const VkDescriptorPoolCreateInfo poolInfo
    {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = 2 + DenoiseSetCount + HistoryCount,
        .poolSizeCount = 2,
        .pPoolSizes    = poolSizes
    };
    if (vkCreateDescriptorPool(m_ctx.device(), &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        showError("Failed to create the GTAO descriptor pool");
        return false;
    }
    return true;
}

// Screen Sized Targets

bool GtaoPass::createWorkingDepth()
{
    const VkImageCreateInfo imageInfo
    {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = WorkingDepthFormat,
        .extent        = { m_width, m_height, 1 },
        .mipLevels     = DepthMips,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    const VmaAllocationCreateInfo alloc{ .usage = VMA_MEMORY_USAGE_AUTO };
    if (vmaCreateImage(m_ctx.allocator(), &imageInfo, &alloc,
                       &m_workingDepth.image, &m_workingDepth.allocation, nullptr) != VK_SUCCESS) {
        showError("Failed to create the GTAO working depth");
        return false;
    }
    m_workingDepth.mipLevels = DepthMips;

    for (uint32_t mip = 0; mip <= DepthMips; ++mip) {
        const bool all = mip == DepthMips;
        const VkImageViewCreateInfo viewInfo
        {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image            = m_workingDepth.image,
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = WorkingDepthFormat,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, all ? 0 : mip, all ? DepthMips : 1, 0, 1 }
        };
        VkImageView &view = all ? m_workingDepth.imageView : m_depthMipViews[mip];
        if (vkCreateImageView(m_ctx.device(), &viewInfo, nullptr, &view) != VK_SUCCESS) {
            showError("Failed to create a GTAO working depth view");
            return false;
        }
    }
    return true;
}

bool GtaoPass::allocateSets()
{
    constexpr uint32_t SetCount = 2 + DenoiseSetCount + HistoryCount;
    std::array<VkDescriptorSetLayout, SetCount> layouts{};
    layouts[0] = m_setLayouts[Prefilter];
    layouts[1] = m_setLayouts[Main];
    std::fill(layouts.begin() + 2, layouts.begin() + 2 + DenoiseSetCount, m_setLayouts[Denoise]);
    std::fill(layouts.begin() + 2 + DenoiseSetCount, layouts.end(), m_setLayouts[Temporal]);

    std::array<VkDescriptorSet, SetCount> sets{};
    const VkDescriptorSetAllocateInfo allocInfo
    {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = m_pool,
        .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
        .pSetLayouts        = layouts.data()
    };
    if (vkAllocateDescriptorSets(m_ctx.device(), &allocInfo, sets.data()) != VK_SUCCESS) {
        showError("Failed to allocate the GTAO descriptor sets");
        return false;
    }
    m_prefilterSet = sets[0];
    m_mainSet      = sets[1];
    std::copy(sets.begin() + 2, sets.begin() + 2 + DenoiseSetCount, m_denoiseSets.begin());
    std::copy(sets.begin() + 2 + DenoiseSetCount, sets.end(), m_temporalSets.begin());
    return true;
}


void GtaoPass::writeImage(VkDescriptorSet set, uint32_t binding, uint32_t element,
                          VkDescriptorType type, VkImageView view, VkImageLayout layout) const
{
    const VkDescriptorImageInfo info
    {
        .sampler     = type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ? m_pointSampler : VK_NULL_HANDLE,
        .imageView   = view,
        .imageLayout = layout
    };
    const VkWriteDescriptorSet write
    {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = set,
        .dstBinding      = binding,
        .dstArrayElement = element,
        .descriptorCount = 1,
        .descriptorType  = type,
        .pImageInfo      = &info
    };
    vkUpdateDescriptorSets(m_ctx.device(), 1, &write, 0, nullptr);
}

bool GtaoPass::createTargets(uint32_t width, uint32_t height)
{
    m_width  = width;
    m_height = height;

    constexpr VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!createWorkingDepth() ||
        !m_ctx.createRenderTarget(width, height, AoFormat, usage, m_aoWorking) ||
        !m_ctx.createRenderTarget(width, height, AoFormat, usage, m_aoTemp) ||
        !m_ctx.createRenderTarget(width, height, AoFormat, usage, m_edges) ||
        // TRANSFER_DST only for disabled path's clear to white.
        !m_ctx.createRenderTarget(width, height, AoFormat, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT, m_final) ||
        !m_ctx.createRenderTarget(width, height, HistoryFormat, usage, m_history[0]) ||
        !m_ctx.createRenderTarget(width, height, HistoryFormat, usage, m_history[1]) ||
        !allocateSets()) {
        showError("Failed to create the GTAO targets");
        return false;
    }

    constexpr VkDescriptorType Sampled = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    constexpr VkDescriptorType Storage = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    constexpr VkImageLayout    General = VK_IMAGE_LAYOUT_GENERAL;

    for (uint32_t mip = 0; mip < DepthMips; ++mip) {
        writeImage(m_prefilterSet, 1, mip, Storage, m_depthMipViews[mip], General);
    }

    writeImage(m_mainSet, 0, 0, Sampled, m_workingDepth.imageView, General);
    writeImage(m_mainSet, 1, 0, Storage, m_aoWorking.imageView, General);
    writeImage(m_mainSet, 2, 0, Storage, m_edges.imageView, General);

    const struct { DenoiseSet set; const GPUImage &src; const GPUImage &dst; } denoise[]
    {
        { WorkingToTemp,  m_aoWorking, m_aoTemp    },
        { TempToWorking,  m_aoTemp,    m_aoWorking },
        { WorkingToFinal, m_aoWorking, m_final     },
        { TempToFinal,    m_aoTemp,    m_final     },
        { History0ToFinal, m_history[0], m_final   },
        { History1ToFinal, m_history[1], m_final   },
    };
    for (const auto &d : denoise) {
        writeImage(m_denoiseSets[d.set], 0, 0, Sampled, d.src.imageView, General);
        writeImage(m_denoiseSets[d.set], 1, 0, Sampled, m_edges.imageView, General);
        writeImage(m_denoiseSets[d.set], 2, 0, Storage, d.dst.imageView, General);
    }

    for (uint32_t i = 0; i < HistoryCount; ++i) {
        writeImage(m_temporalSets[i], 0, 0, Sampled, m_aoWorking.imageView,      General);
        writeImage(m_temporalSets[i], 1, 0, Sampled, m_history[i].imageView,      General);
        writeImage(m_temporalSets[i], 2, 0, Storage, m_history[i ^ 1u].imageView, General);
        writeImage(m_temporalSets[i], 3, 0, Sampled, m_workingDepth.imageView,   General);
    }

    m_historyIndex = 0;
    m_historyReady = false;
    return true;
}

void GtaoPass::setDepthView(VkImageView depthView)
{
    writeImage(m_prefilterSet, 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
               depthView, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
}

void GtaoPass::destroyTargets()
{
    const VkDevice device = m_ctx.device();
    if (!device) {
        return;
    }
    for (VkImageView &view : m_depthMipViews) {
        vkDestroyImageView(device, view, nullptr);
        view = nullptr;
    }
    for (GPUImage *image : { &m_workingDepth, &m_aoWorking, &m_aoTemp, &m_edges, &m_final,
                             &m_history[0], &m_history[1] }) {
        m_ctx.destroyImage(*image);
    }
    if (m_pool) {
        vkResetDescriptorPool(device, m_pool, 0);
    }
    m_prefilterSet = nullptr;
    m_mainSet      = nullptr;
    m_denoiseSets.fill(nullptr);
    m_temporalSets.fill(nullptr);
    m_historyReady = false;
}

bool GtaoPass::createPipelines()
{
    return createPipeline(Prefilter, m_pipelines[Prefilter]) &&
           createPipeline(Main,      m_pipelines[Main]) &&
           createPipeline(Denoise,   m_pipelines[Denoise]) &&
           createPipeline(Temporal,  m_pipelines[Temporal]);
}

bool GtaoPass::createPipeline(Stage stage, VkPipeline &outPipeline)
{
    const VkComputePipelineCreateInfo info
    {
        .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = m_shaders[stage],
            .pName  = "main"
        },
        .layout = m_layouts[stage],
    };
    if (vkCreateComputePipelines(m_ctx.device(), nullptr, 1, &info, nullptr, &outPipeline) != VK_SUCCESS) {
        showError("Failed to create a GTAO compute pipeline");
        return false;
    }
    return true;
}
void GtaoPass::destroy()
{
    const VkDevice device = m_ctx.device();
    if (!device) {
        return;
    }
    destroyTargets();
    for (uint32_t i = 0; i < StageCount; ++i) {
        vkDestroyPipeline(device, m_pipelines[i], nullptr);
        vkDestroyShaderModule(device, m_shaders[i], nullptr);
        vkDestroyPipelineLayout(device, m_layouts[i], nullptr);
        vkDestroyDescriptorSetLayout(device, m_setLayouts[i], nullptr);
        m_pipelines[i]  = nullptr;
        m_shaders[i]    = nullptr;
        m_layouts[i]    = nullptr;
        m_setLayouts[i] = nullptr;
    }
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    m_pool = nullptr;
    vkDestroySampler(device, m_pointSampler, nullptr);
    m_pointSampler = nullptr;
}


// =================================================== //
//                    Per Frame                        //
// =================================================== //

void GtaoPass::update(const glm::mat4 &projection, const glm::mat4 &view, const glm::mat4 &viewProj) {
    // XeGTAO presets: slices x steps per side.
    static constexpr uint32_t Slices[] = { 1, 2, 3, 9 };
    static constexpr uint32_t Steps[]  = { 2, 2, 3, 3 };
    const int quality = std::clamp(m_settings.quality, 0, 3);

    const glm::vec2 tanHalfFov{ 1.0f / projection[0][0], 1.0f / projection[1][1] };

    GtaoConstants &c    = m_constants;
    c.viewportSize      = { static_cast<int>(m_width), static_cast<int>(m_height) };
    c.viewportPixelSize = { 1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height) };
    c.depthUnpack       = { projection[3][2], projection[2][2] };
    c.uvToViewMul       = { 2.0f * tanHalfFov.x, -2.0f * tanHalfFov.y };
    c.uvToViewAdd       = { -tanHalfFov.x, tanHalfFov.y };
    c.uvToViewMulPixel  = c.uvToViewMul * c.viewportPixelSize;
    c.effectRadius      = m_settings.radius * 1.457f;   // XeGTAO's radius multiplier
    c.falloffRange      = m_settings.falloffRange;
    c.finalPower        = m_settings.finalPower;
    c.denoiseBeta       = m_settings.denoisePasses == 0 ? 1e4f : 1.2f;   // 1e4: centre only
    c.mipSamplingOffset = m_settings.mipSamplingOffset;
    c.sliceCount        = Slices[quality];
    c.stepsPerSlice     = Steps[quality];

    const bool temporal    = m_settings.enabled && m_settings.temporal;
    const bool cameraMoved = viewProj != m_prevViewProj;

    GtaoTemporalConstants &t = m_temporal;
    t.viewportSize   = c.viewportSize;
    t.uvToViewMul    = c.uvToViewMul;
    t.uvToViewAdd    = c.uvToViewAdd;
    t.reproject      = m_prevViewProj * glm::inverse(view) * glm::scale(glm::mat4(1.0f), { 1.0f, 1.0f, -1.0f });
    t.maxHistory     = cameraMoved ? 10.0f : 32.0f;
    t.depthTolerance = 0.03f;
    t.historyValid   = temporal && m_historyReady && m_settings == m_prevSettings ? 1u : 0u;

    m_temporalFrame = temporal ? m_temporalFrame + 1 : 0;
    c.noiseIndex    = m_temporalFrame % 64u;

    m_prevViewProj = viewProj;
    m_prevSettings = m_settings;
}

void GtaoPass::dispatch(VkCommandBuffer cmd, Stage stage, VkDescriptorSet set,
                        uint32_t groupsX, uint32_t groupsY) const
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelines[stage]);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_layouts[stage], 0, 1, &set, 0, nullptr);
    if (stage == Temporal) {
        vkCmdPushConstants(cmd, m_layouts[stage], VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_temporal), &m_temporal);
    } else {
        vkCmdPushConstants(cmd, m_layouts[stage], VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants), &m_constants);
    }
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

void GtaoPass::record(VkCommandBuffer cmd)
{
    if (!m_settings.enabled) {
        m_historyReady = false;
        recordDisabled(cmd);
        return;
    }

    const bool     temporal   = m_settings.temporal;
    const uint32_t readIndex  = m_historyIndex;
    const uint32_t writeIndex = readIndex ^ 1u;

    std::array<vkutil::ImageBarrier, 7> begin
    {
        discardToGeneral(m_workingDepth.image, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, DepthMips),
        discardToGeneral(m_aoWorking.image,    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT),
        discardToGeneral(m_aoTemp.image,       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT),
        discardToGeneral(m_edges.image,        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT),
        discardToGeneral(m_final.image,        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT),
    };
    size_t beginCount = 5;
    if (temporal) {
        begin[beginCount++] = discardToGeneral(m_history[writeIndex].image, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        if (m_temporal.historyValid == 0) {
            begin[beginCount++] = discardToGeneral(m_history[readIndex].image, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        }
    }
    vkutil::imageBarriers(cmd, std::span(begin.data(), beginCount));

    const uint32_t gx = groupsFor(m_width, GroupSize);
    const uint32_t gy = groupsFor(m_height, GroupSize);

    // 16x16 pixels per group: 8x8 threads, 2x2 each.
    m_constants.finalPass = 0;
    dispatch(cmd, Prefilter, m_prefilterSet, groupsFor(m_width, 16), groupsFor(m_height, 16));
    vkutil::imageBarrier(cmd, writeToRead(m_workingDepth.image, DepthMips));

    dispatch(cmd, Main, m_mainSet, gx, gy);
    const std::array<vkutil::ImageBarrier, 2> afterMain
    {
        writeToRead(m_aoWorking.image), writeToRead(m_edges.image)
    };
    vkutil::imageBarriers(cmd, afterMain);

    if (temporal) {
        m_constants.finalPass = 0;
        dispatch(cmd, Temporal, m_temporalSets[readIndex], gx, gy);
        vkutil::imageBarrier(cmd, writeToRead(m_history[writeIndex].image));

        m_constants.finalPass = 1;
        dispatch(cmd, Denoise, m_denoiseSets[writeIndex == 0 ? History0ToFinal : History1ToFinal], gx, gy);

        m_historyIndex = writeIndex;
        m_historyReady = true;
        return;
    }
    m_historyReady = false;

    const uint32_t passes = static_cast<uint32_t>(std::clamp(m_settings.denoisePasses, 1, 3));
    for (uint32_t pass = 0; pass < passes; ++pass) {
        const bool last       = pass + 1 == passes;
        const bool srcWorking = (pass & 1u) == 0;
        const DenoiseSet set  = last ? (srcWorking ? WorkingToFinal : TempToFinal)
                                     : (srcWorking ? WorkingToTemp  : TempToWorking);

        m_constants.finalPass = last ? 1u : 0u;
        dispatch(cmd, Denoise, m_denoiseSets[set], gx, gy);
        if (!last) {
            vkutil::imageBarrier(cmd, writeToRead(srcWorking ? m_aoTemp.image : m_aoWorking.image));
        }
    }
}

void GtaoPass::recordDisabled(VkCommandBuffer cmd) const
{
    // pbr.frag samples the result either way; white means "unoccluded".
    // Cleared in GENERAL, so makeResultReadable() is the same on both paths.
    const VkImageSubresourceRange range = colorRange(1);
    vkutil::imageBarrier(cmd, {
        .image     = m_final.image,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcStage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstStage  = VK_PIPELINE_STAGE_2_CLEAR_BIT,
        .dstAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .range     = range,
    });
    const VkClearColorValue white{ .float32 = { 1.0f, 1.0f, 1.0f, 1.0f } };
    vkCmdClearColorImage(cmd, m_final.image, VK_IMAGE_LAYOUT_GENERAL, &white, 1, &range);
}

void GtaoPass::makeResultReadable(VkCommandBuffer cmd) const
{
    vkutil::imageBarrier(cmd, {
        .image     = m_final.image,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcStage  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT,
        .srcAccess = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .range     = colorRange(1),
    });
}