#include "EnvironmentMap.h"

#include <cmath>
#include <utility>

#include "../shaders/ShaderCompiler.h"
#include "../../common/errors.h"
#include "../../common/vkbarrier.h"

using namespace render;

bool EnvironmentMap::load(const BakeContext& ctx,
                          VkImageView equirectView,
                          VkSampler equirectSampler,
                          const EnvironmentSettings& settings) {
    if (!ctx.device || !ctx.allocator || !ctx.uploader) {
        showError("EnvironmentMap::load called with an incomplete BakeContext");
        return false;
    }
    if (!equirectView || !equirectSampler) {
        showError("EnvironmentMap::load called without a source equirect");
        return false;
    }

    m_ctx      = ctx;
    m_settings = settings;

    const uint32_t skyMips = settings.generateMips
        ? uint32_t(std::floor(std::log2(float(settings.cubeSize)))) + 1u
        : 1u;

    if (!createSampler())                                         return false;
    if (!createCubemap(m_skybox, settings.cubeSize, skyMips))      return false;
    if (!createCubemap(m_irradiance, settings.irradianceSize, 1))  return false;
    if (!createPipelines())                                        return false;

    VkCommandBuffer cmd = m_ctx.uploader->begin();
    if (!cmd) {
        showError("Could not begin an upload command buffer for the IBL bake");
        return false;
    }

    recordEquirectToCube(cmd, equirectView, equirectSampler);
    recordIrradiance(cmd);

    const Uploader::Ticket ticket = m_ctx.uploader->submit();
    if (!ticket) {
        showError("IBL bake submit failed");
        return false;
    }

    m_ctx.uploader->wait(ticket);
    destroyBakeOnly();
    return true;
}

bool EnvironmentMap::createCubemap(Cubemap& out, uint32_t size, uint32_t mips) {
    out.size   = size;
    out.mips   = mips;
    out.format = CubemapFORMAT;

    const VkImageCreateInfo imgInfo{
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = out.format,
        .extent        = { size, size, 1 },
        .mipLevels     = mips,
        .arrayLayers   = 6,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo alloc{ .usage = VMA_MEMORY_USAGE_AUTO };
    if (vmaCreateImage(m_ctx.allocator, &imgInfo, &alloc,
                       &out.image, &out.allocation, nullptr) != VK_SUCCESS) {
        showError("Could not create cubemap image");
        return false;
    }

    const VkImageViewCreateInfo cubeView{
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = out.image,
        .viewType         = VK_IMAGE_VIEW_TYPE_CUBE,
        .format           = out.format,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 6 },
    };
    if (vkCreateImageView(m_ctx.device, &cubeView, nullptr, &out.cubeView) != VK_SUCCESS) {
        showError("Could not create cubemap sampling view");
        return false;
    }

    out.mipStorageViews.resize(mips, VK_NULL_HANDLE);
    for (uint32_t m = 0; m < mips; ++m) {
        const VkImageViewCreateInfo sv{
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image            = out.image,
            .viewType         = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            .format           = out.format,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, m, 1, 0, 6 },
        };
        if (vkCreateImageView(m_ctx.device, &sv, nullptr, &out.mipStorageViews[m]) != VK_SUCCESS) {
            showError("Could not create cubemap storage view");
            return false;
        }
    }
    return true;
}

void EnvironmentMap::recordPrefilter(VkCommandBuffer cmd)
{

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_prefilterPipeline);

    for (uint32_t mip = 1; mip < m_skybox.mips; ++mip) {
        const uint32_t mipSize = std::max(1u, m_skybox.size >> mip);

        const VkDescriptorSet set = allocateSet(m_prefilterSetLayout);
        if (!set) return;
        // binding 0: { m_sampler, m_skyboxMip0View, SHADER_READ_ONLY_OPTIMAL }
        // binding 1: { VK_NULL_HANDLE, m_skybox.mipStorageViews[mip], GENERAL }

        const PrefilterPush push{
            .roughness   = float(mip) / float(m_skybox.mips - 1),
            .mipSize     = mipSize,
            .sampleCount = 128,
            .sourceSize  = float(m_skybox.size),
        };
        vkCmdPushConstants(cmd, m_prefilterPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(push), &push);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_prefilterPipeLayout, 0, 1, &set, 0, nullptr);

        const uint32_t groups = (mipSize + 7) / 8;
        vkCmdDispatch(cmd, groups, groups, 6);
    }

    // mips 1..n-1 -> SHADER_READ_ONLY_OPTIMAL
}

bool EnvironmentMap::createSampler() {
    const VkSamplerCreateInfo cube{
        .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter    = VK_FILTER_LINEAR,
        .minFilter    = VK_FILTER_LINEAR,
        .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .minLod       = 0.0f,
        .maxLod       = m_settings.generateMips ? VK_LOD_CLAMP_NONE : 0.25f,
    };
    if (vkCreateSampler(m_ctx.device, &cube, nullptr, &m_sampler) != VK_SUCCESS) {
        showError("Could not create cubemap sampler");
        return false;
    }
    return true;
}

bool EnvironmentMap::createPipelines() {
    const VkDescriptorPoolSize sizes[]{
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          8 },
    };
    const VkDescriptorPoolCreateInfo poolInfo{
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = 8,
        .poolSizeCount = 2,
        .pPoolSizes    = sizes,
    };
    if (vkCreateDescriptorPool(m_ctx.device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        showError("Could not create the IBL descriptor pool");
        return false;
    }

    const VkDescriptorSetLayoutBinding bindings[]{
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
    };
    const VkDescriptorSetLayoutCreateInfo layoutInfo{
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings    = bindings,
    };
    if (vkCreateDescriptorSetLayout(m_ctx.device, &layoutInfo, nullptr, &m_equirectSetLayout) != VK_SUCCESS ||
        vkCreateDescriptorSetLayout(m_ctx.device, &layoutInfo, nullptr, &m_convolveSetLayout) != VK_SUCCESS) {
        showError("Could not create an IBL descriptor set layout");
        return false;
    }

    auto makePipeline = [&](const char* file,
                            VkDescriptorSetLayout setLayout,
                            VkPipelineLayout& outLayout,
                            VkPipeline& outPipeline) -> bool {
        const VkPipelineLayoutCreateInfo plInfo{
            .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts    = &setLayout,
        };
        if (vkCreatePipelineLayout(m_ctx.device, &plInfo, nullptr, &outLayout) != VK_SUCCESS) {
            showError("Could not create an IBL pipeline layout");
            return false;
        }

        VkShaderModule module = compileShaderModule(m_ctx.device, file, shaderc_compute_shader);
        if (!module) return false;

        const VkComputePipelineCreateInfo pipeInfo{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = {
                .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = module,
                .pName  = "main",
            },
            .layout = outLayout,
        };
        const VkResult r = vkCreateComputePipelines(m_ctx.device, VK_NULL_HANDLE, 1,
                                                    &pipeInfo, nullptr, &outPipeline);
        vkDestroyShaderModule(m_ctx.device, module, nullptr);
        if (r != VK_SUCCESS) {
            showError("Could not create an IBL compute pipeline");
            return false;
        }
        return true;
    };

    return makePipeline("ibl/equirect_to_cube.comp", m_equirectSetLayout,
                        m_equirectPipeLayout, m_equirectPipeline)
        && makePipeline("ibl/irradiance.comp", m_convolveSetLayout,
                        m_convolvePipeLayout, m_irradiancePipeline);
}

VkDescriptorSet EnvironmentMap::allocateSet(VkDescriptorSetLayout layout) {
    const VkDescriptorSetAllocateInfo info{
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = m_descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &layout,
    };
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(m_ctx.device, &info, &set) != VK_SUCCESS)
        showError("Could not allocate an IBL descriptor set");
    return set;
}

void EnvironmentMap::recordEquirectToCube(VkCommandBuffer cmd,
                                          VkImageView src,
                                          VkSampler srcSampler) {
    const VkImageSubresourceRange all{ VK_IMAGE_ASPECT_COLOR_BIT, 0, m_skybox.mips, 0, 6 };

    vkutil::imageBarrier(cmd, {
        .image     = m_skybox.image,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcStage  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        .dstStage  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccess = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .range     = all,
    });

    const VkDescriptorSet set = allocateSet(m_equirectSetLayout);
    if (!set) return;

    const VkDescriptorImageInfo srcInfo{ srcSampler, src,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    const VkDescriptorImageInfo dstInfo{ VK_NULL_HANDLE, m_skybox.mipStorageViews[0],
                                         VK_IMAGE_LAYOUT_GENERAL };
    const VkWriteDescriptorSet writes[]{
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 0,
          .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .pImageInfo = &srcInfo },
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 1,
          .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          .pImageInfo = &dstInfo },
    };
    vkUpdateDescriptorSets(m_ctx.device, 2, writes, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_equirectPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_equirectPipeLayout,
                            0, 1, &set, 0, nullptr);

    const uint32_t groups = (m_skybox.size + 7) / 8;
    vkCmdDispatch(cmd, groups, groups, 6);

    vkutil::imageBarrier(cmd, {
        .image     = m_skybox.image,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcStage  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccess = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .dstStage  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                   | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .range     = all,
    });
}

void EnvironmentMap::recordIrradiance(VkCommandBuffer cmd) {
    const VkImageSubresourceRange all{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };

    vkutil::imageBarrier(cmd, {
        .image     = m_irradiance.image,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcStage  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        .dstStage  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccess = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .range     = all,
    });

    const VkDescriptorSet set = allocateSet(m_convolveSetLayout);
    if (!set) return;

    const VkDescriptorImageInfo srcInfo{ m_sampler, m_skybox.cubeView,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    const VkDescriptorImageInfo dstInfo{ VK_NULL_HANDLE, m_irradiance.mipStorageViews[0],
                                         VK_IMAGE_LAYOUT_GENERAL };
    const VkWriteDescriptorSet writes[]{
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 0,
          .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .pImageInfo = &srcInfo },
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 1,
          .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          .pImageInfo = &dstInfo },
    };
    vkUpdateDescriptorSets(m_ctx.device, 2, writes, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_irradiancePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_convolvePipeLayout,
                            0, 1, &set, 0, nullptr);

    const uint32_t groups = (m_irradiance.size + 7) / 8;
    vkCmdDispatch(cmd, groups, groups, 6);

    vkutil::imageBarrier(cmd, {
        .image     = m_irradiance.image,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcStage  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccess = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .dstStage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .range     = all,
    });
}

void EnvironmentMap::destroyCubemap(Cubemap& c) {
    if (!m_ctx.device) return;
    for (VkImageView v : c.mipStorageViews)
        vkDestroyImageView(m_ctx.device, v, nullptr);
    c.mipStorageViews.clear();
    vkDestroyImageView(m_ctx.device, c.cubeView, nullptr);
    if (c.image) vmaDestroyImage(m_ctx.allocator, c.image, c.allocation);
    c = {};
}

void EnvironmentMap::destroyBakeOnly() {
    if (!m_ctx.device) return;
    vkDestroyPipeline(m_ctx.device, m_equirectPipeline, nullptr);
    vkDestroyPipeline(m_ctx.device, m_irradiancePipeline, nullptr);
    vkDestroyPipelineLayout(m_ctx.device, m_equirectPipeLayout, nullptr);
    vkDestroyPipelineLayout(m_ctx.device, m_convolvePipeLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_ctx.device, m_equirectSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_ctx.device, m_convolveSetLayout, nullptr);
    vkDestroyDescriptorPool(m_ctx.device, m_descriptorPool, nullptr);

    m_equirectPipeline   = VK_NULL_HANDLE;
    m_irradiancePipeline = VK_NULL_HANDLE;
    m_equirectPipeLayout = VK_NULL_HANDLE;
    m_convolvePipeLayout = VK_NULL_HANDLE;
    m_equirectSetLayout  = VK_NULL_HANDLE;
    m_convolveSetLayout  = VK_NULL_HANDLE;
    m_descriptorPool     = VK_NULL_HANDLE;
}

void EnvironmentMap::destroy() {
    if (!m_ctx.device) return;
    destroyBakeOnly();

    destroyCubemap(m_skybox);
    destroyCubemap(m_irradiance);

    vkDestroySampler(m_ctx.device, m_sampler, nullptr);
    m_sampler = VK_NULL_HANDLE;

    m_ctx = {};
}

EnvironmentMap::~EnvironmentMap() {
    destroy();
}

EnvironmentMap::EnvironmentMap(EnvironmentMap&& other) noexcept {
    *this = std::move(other);
}

EnvironmentMap& EnvironmentMap::operator=(EnvironmentMap&& other) noexcept {
    if (this == &other) return *this;
    destroy();

    m_ctx        = std::exchange(other.m_ctx, {});
    m_settings   = other.m_settings;
    m_skybox     = std::exchange(other.m_skybox, {});
    m_irradiance = std::exchange(other.m_irradiance, {});
    m_sampler    = std::exchange(other.m_sampler, VK_NULL_HANDLE);

    m_descriptorPool     = std::exchange(other.m_descriptorPool, VK_NULL_HANDLE);
    m_equirectSetLayout  = std::exchange(other.m_equirectSetLayout, VK_NULL_HANDLE);
    m_convolveSetLayout  = std::exchange(other.m_convolveSetLayout, VK_NULL_HANDLE);
    m_equirectPipeLayout = std::exchange(other.m_equirectPipeLayout, VK_NULL_HANDLE);
    m_convolvePipeLayout = std::exchange(other.m_convolvePipeLayout, VK_NULL_HANDLE);
    m_equirectPipeline   = std::exchange(other.m_equirectPipeline, VK_NULL_HANDLE);
    m_irradiancePipeline = std::exchange(other.m_irradiancePipeline, VK_NULL_HANDLE);
    return *this;
}