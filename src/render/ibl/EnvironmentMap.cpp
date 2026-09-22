#include "EnvironmentMap.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "../shaders/ShaderCompiler.h"
#include "../../common/errors.h"
#include "../core/vkbarrier.h"

using namespace render;

bool EnvironmentMap::load(const BakeContext& ctx,
                          VkImageView equirectView,
                          VkSampler equirectSampler,
                          const EnvironmentSettings& settings,
                          std::vector<std::string>* shaderDeps,
                          std::string* shaderError) {
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

    const uint32_t prefilterMaxMips = uint32_t(std::floor(std::log2(float(settings.prefilterSize)))) + 1u;
    const uint32_t prefilterMips    = std::clamp(settings.prefilterMips, 1u, prefilterMaxMips);

    const auto fail = [this] { destroy(); return false; };


    const VkImageUsageFlags skyUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    if (!createSampler())                                                        return fail();
    if (!createCubemap(m_skybox, settings.cubeSize, skyMips, skyUsage))          return fail();
    if (!createCubemap(m_irradiance, settings.irradianceSize, 1))                return fail();
    if (!createCubemap(m_prefilter, settings.prefilterSize, prefilterMips))      return fail();
    if (!createBrdfLut(settings.brdfLutSize)) return fail();
    if (!createPipelines(shaderDeps, shaderError))                return fail();

    VkCommandBuffer cmd = m_ctx.uploader->begin();
    if (!cmd) {
        showError("Could not begin an upload command buffer for the IBL bake");
        return fail();
    }

    recordEquirectToCube(cmd, equirectView, equirectSampler);
    recordSkyMips(cmd);
    recordIrradiance(cmd);
    recordPrefilter(cmd);
    recordBrdfLut(cmd);

    const Uploader::Ticket ticket = m_ctx.uploader->submit();
    if (!ticket) {
        showError("IBL bake submit failed");
        return fail();
    }

    m_ctx.uploader->wait(ticket);
    destroyBakeOnly();
    return true;
}

bool EnvironmentMap::createCubemap(Cubemap& out, uint32_t size, uint32_t mips,
                                   VkImageUsageFlags extraUsage) {
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
        .usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | extraUsage,
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
    if (!m_prefilterPipeline) return;

    const VkImageSubresourceRange all{ VK_IMAGE_ASPECT_COLOR_BIT, 0, m_prefilter.mips, 0, 6 };

    vkutil::imageBarrier(cmd, {
        .image     = m_prefilter.image,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcStage  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        .dstStage  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccess = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .range     = all,
    });

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_prefilterPipeline);

    const float lastMip = float(std::max(m_prefilter.mips, 2u) - 1);
    for (uint32_t mip = 0; mip < m_prefilter.mips; ++mip) {
        const uint32_t mipSize = std::max(1u, m_prefilter.size >> mip);

        const VkDescriptorSet set = allocateSet(m_prefilterSetLayout);
        if (!set) return;

        const VkDescriptorImageInfo srcInfo{ m_sampler, m_skybox.cubeView,
                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        const VkDescriptorImageInfo dstInfo{ VK_NULL_HANDLE, m_prefilter.mipStorageViews[mip],
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

        const PrefilterPush push{
            .roughness   = m_prefilter.mips > 1 ? float(mip) / lastMip : 0.0f,
            .mipSize     = mipSize,
            .sampleCount = 1024,
            .sourceSize  = float(m_skybox.size),
        };
        vkCmdPushConstants(cmd, m_prefilterPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(push), &push);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_prefilterPipeLayout, 0, 1, &set, 0, nullptr);

        const uint32_t groups = (mipSize + 7) / 8;
        vkCmdDispatch(cmd, groups, groups, 6);
    }

    vkutil::imageBarrier(cmd, {
        .image     = m_prefilter.image,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcStage  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccess = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .dstStage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .range     = all,
    });
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
        .maxLod       = VK_LOD_CLAMP_NONE,
    };
    if (vkCreateSampler(m_ctx.device, &cube, nullptr, &m_sampler) != VK_SUCCESS) {
        showError("Could not create cubemap sampler");
        return false;
    }
    return true;
}

bool EnvironmentMap::createBrdfLut(uint32_t size)
{
    m_brdfSize = size;

    const VkImageCreateInfo imgInfo{
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = VK_FORMAT_R16G16_SFLOAT,
        .extent        = { size, size, 1 },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo alloc{ .usage = VMA_MEMORY_USAGE_AUTO };
    if (vmaCreateImage(m_ctx.allocator, &imgInfo, &alloc,
                       &m_brdfImage, &m_brdfAllocation, nullptr) != VK_SUCCESS) {
        showError("Could not create the BRDF LUT image");
        return false;
    }

    const VkImageViewCreateInfo viewInfo{
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = m_brdfImage,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = VK_FORMAT_R16G16_SFLOAT,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    if (vkCreateImageView(m_ctx.device, &viewInfo, nullptr, &m_brdfView) != VK_SUCCESS) {
        showError("Could not create the BRDF LUT view");
        return false;
    }
    return true;
}

void EnvironmentMap::recordBrdfLut(VkCommandBuffer cmd)
{
    const VkImageSubresourceRange all{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    vkutil::imageBarrier(cmd, {
        .image     = m_brdfImage,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcStage  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        .dstStage  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccess = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .range     = all,
    });

    const VkDescriptorSet set = allocateSet(m_brdfSetLayout);
    if (!set) return;

    const VkDescriptorImageInfo dstInfo{ VK_NULL_HANDLE, m_brdfView, VK_IMAGE_LAYOUT_GENERAL };
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 0,
        .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &dstInfo,
    };
    vkUpdateDescriptorSets(m_ctx.device, 1, &write, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_brdfPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_brdfPipeLayout,
                            0, 1, &set, 0, nullptr);

    const BrdfLutPush push{ .size = m_brdfSize, .sampleCount = 1024 };
    vkCmdPushConstants(cmd, m_brdfPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(push), &push);

    const uint32_t groups = (m_brdfSize + 7) / 8;
    vkCmdDispatch(cmd, groups, groups, 1);

    vkutil::imageBarrier(cmd, {
        .image     = m_brdfImage,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcStage  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccess = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .dstStage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .range     = all,
    });
}

bool EnvironmentMap::createPipelines(std::vector<std::string>* shaderDeps, std::string* shaderError) {

    // equirect + irradiance + BRDF LUT, plus one set per prefilter level
    // (level 0 included: it is baked too now).
    const uint32_t setCount = 3 + m_prefilter.mips;

    const VkDescriptorPoolSize sizes[]{
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, setCount - 1 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          setCount     },
    };

    const VkDescriptorPoolCreateInfo poolInfo{
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = setCount,
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
        vkCreateDescriptorSetLayout(m_ctx.device, &layoutInfo, nullptr, &m_convolveSetLayout) != VK_SUCCESS ||
        vkCreateDescriptorSetLayout(m_ctx.device, &layoutInfo, nullptr, &m_prefilterSetLayout) != VK_SUCCESS) {
        showError("Could not create an IBL descriptor set layout");
        return false;
    }

    if (!createPipeline("ibl/equirect_to_cube.comp", m_equirectSetLayout, 0,
                        m_equirectPipeLayout, m_equirectPipeline, shaderDeps, shaderError))
        return false;
    if (!createPipeline("ibl/irradiance.comp", m_convolveSetLayout, sizeof(IrradiancePush),
                        m_convolvePipeLayout, m_irradiancePipeline, shaderDeps, shaderError))
        return false;
    if (!createPipeline("ibl/prefilter.comp", m_prefilterSetLayout, sizeof(PrefilterPush),
                        m_prefilterPipeLayout, m_prefilterPipeline, shaderDeps, shaderError))
        return false;

    const VkDescriptorSetLayoutBinding brdfBinding{
        0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr
    };
    const VkDescriptorSetLayoutCreateInfo brdfLayoutInfo{
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = &brdfBinding,
    };
    if (vkCreateDescriptorSetLayout(m_ctx.device, &brdfLayoutInfo, nullptr, &m_brdfSetLayout) != VK_SUCCESS) {
        showError("Could not create the BRDF LUT descriptor set layout");
        return false;
    }
    if (!createPipeline("ibl/brdf_lut.comp", m_brdfSetLayout, sizeof(BrdfLutPush),
                        m_brdfPipeLayout, m_brdfPipeline, shaderDeps, shaderError)) {
        if (!shaderError) {
            showError("Could not create an BRDF pipeline with brdf_lut.comp shader");
        }
        return false;

    }
    return true;
}

bool EnvironmentMap::createPipeline(const char* file,
                                    VkDescriptorSetLayout setLayout,
                                    uint32_t pushConstantSize,
                                    VkPipelineLayout& outLayout,
                                    VkPipeline& outPipeline,
                                    std::vector<std::string>* shaderDeps,
                                    std::string* shaderError) {
    const VkPushConstantRange pushRange{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset     = 0,
        .size       = pushConstantSize,
    };
    const VkPipelineLayoutCreateInfo plInfo{
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &setLayout,
        .pushConstantRangeCount = pushConstantSize ? 1u : 0u,
        .pPushConstantRanges    = pushConstantSize ? &pushRange : nullptr,
    };
    if (vkCreatePipelineLayout(m_ctx.device, &plInfo, nullptr, &outLayout) != VK_SUCCESS) {
        showError("Could not create an IBL pipeline layout");
        return false;
    }

    VkShaderModule module = compileShaderModule(m_ctx.device, file, shaderc_compute_shader,
                                                shaderError, shaderDeps);
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
    // Mip 0 only.
    const VkImageSubresourceRange all{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };

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
    // Mip 0 stays GENERAL; recordSkyMips takes it from here.
}

void EnvironmentMap::recordSkyMips(VkCommandBuffer cmd)
{
    // generateMips wants every level in TRANSFER_DST with level 0 visible to
    // the blit. Level 0 was just written by the equirect compute pass.
    vkutil::ImageBarrier toTransfer[2]{
        {
            .image     = m_skybox.image,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcStage  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccess = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .dstStage  = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .dstAccess = VK_ACCESS_2_TRANSFER_READ_BIT,
            .range     = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 },
        },
        {
            .image     = m_skybox.image,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcStage  = VK_PIPELINE_STAGE_2_NONE,
            .dstStage  = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .dstAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .range     = { VK_IMAGE_ASPECT_COLOR_BIT, 1, m_skybox.mips - 1, 0, 6 },
        },
    };
    vkutil::imageBarriers(cmd, { toTransfer, m_skybox.mips > 1 ? 2u : 1u });

    vkutil::generateMips(cmd, {
        .image      = m_skybox.image,
        .width      = m_skybox.size,
        .height     = m_skybox.size,
        .mipLevels  = m_skybox.mips,
        .layerCount = 6,
        .dstStage   = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccess  = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
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

    // irradiance.comp takes ~16k samples per texel over a hemisphere, about the
    // density of a 64x64 face. Reading a finer mip aliases the sun.
    const IrradiancePush push{
        .sourceLod = std::max(0.0f, std::log2(float(m_skybox.size) / 64.0f)),
    };
    vkCmdPushConstants(cmd, m_convolvePipeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(push), &push);

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
    vkDestroyPipeline(m_ctx.device, m_prefilterPipeline, nullptr);
    vkDestroyPipelineLayout(m_ctx.device, m_equirectPipeLayout, nullptr);
    vkDestroyPipelineLayout(m_ctx.device, m_convolvePipeLayout, nullptr);
    vkDestroyPipelineLayout(m_ctx.device, m_prefilterPipeLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_ctx.device, m_equirectSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_ctx.device, m_convolveSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_ctx.device, m_prefilterSetLayout, nullptr);
    vkDestroyDescriptorPool(m_ctx.device, m_descriptorPool, nullptr);
    vkDestroyPipeline(m_ctx.device, m_brdfPipeline, nullptr);
    vkDestroyPipelineLayout(m_ctx.device, m_brdfPipeLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_ctx.device, m_brdfSetLayout,nullptr);

    m_equirectPipeline    = VK_NULL_HANDLE;
    m_irradiancePipeline  = VK_NULL_HANDLE;
    m_prefilterPipeline   = VK_NULL_HANDLE;
    m_equirectPipeLayout  = VK_NULL_HANDLE;
    m_convolvePipeLayout  = VK_NULL_HANDLE;
    m_prefilterPipeLayout = VK_NULL_HANDLE;
    m_equirectSetLayout   = VK_NULL_HANDLE;
    m_convolveSetLayout   = VK_NULL_HANDLE;
    m_prefilterSetLayout  = VK_NULL_HANDLE;
    m_descriptorPool      = VK_NULL_HANDLE;
    m_brdfPipeline   = VK_NULL_HANDLE;
    m_brdfPipeLayout = VK_NULL_HANDLE;
    m_brdfSetLayout  = VK_NULL_HANDLE;
}

void EnvironmentMap::destroy() {
    if (!m_ctx.device) return;
    destroyBakeOnly();

    destroyCubemap(m_skybox);
    destroyCubemap(m_irradiance);
    destroyCubemap(m_prefilter);

    vkDestroyImageView(m_ctx.device, m_brdfView, nullptr);
    m_brdfView = VK_NULL_HANDLE;
    if (m_brdfImage) {
        vmaDestroyImage(m_ctx.allocator, m_brdfImage, m_brdfAllocation);
        m_brdfImage      = VK_NULL_HANDLE;
        m_brdfAllocation = VK_NULL_HANDLE;
    }

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
    m_prefilter  = std::exchange(other.m_prefilter, {});
    m_sampler    = std::exchange(other.m_sampler, VK_NULL_HANDLE);

    m_descriptorPool     = std::exchange(other.m_descriptorPool, VK_NULL_HANDLE);
    m_equirectSetLayout  = std::exchange(other.m_equirectSetLayout, VK_NULL_HANDLE);
    m_convolveSetLayout  = std::exchange(other.m_convolveSetLayout, VK_NULL_HANDLE);
    m_equirectPipeLayout = std::exchange(other.m_equirectPipeLayout, VK_NULL_HANDLE);
    m_convolvePipeLayout = std::exchange(other.m_convolvePipeLayout, VK_NULL_HANDLE);
    m_equirectPipeline   = std::exchange(other.m_equirectPipeline, VK_NULL_HANDLE);
    m_irradiancePipeline = std::exchange(other.m_irradiancePipeline, VK_NULL_HANDLE);

    m_prefilterSetLayout  = std::exchange(other.m_prefilterSetLayout, VK_NULL_HANDLE);
    m_prefilterPipeLayout = std::exchange(other.m_prefilterPipeLayout, VK_NULL_HANDLE);
    m_prefilterPipeline   = std::exchange(other.m_prefilterPipeline, VK_NULL_HANDLE);

    m_brdfImage           = std::exchange(other.m_brdfImage, VK_NULL_HANDLE);
    m_brdfAllocation      = std::exchange(other.m_brdfAllocation, VK_NULL_HANDLE);
    m_brdfView            = std::exchange(other.m_brdfView, VK_NULL_HANDLE);
    m_brdfSize            = std::exchange(other.m_brdfSize, 0u);
    m_brdfSetLayout       = std::exchange(other.m_brdfSetLayout, VK_NULL_HANDLE);
    m_brdfPipeLayout      = std::exchange(other.m_brdfPipeLayout, VK_NULL_HANDLE);
    m_brdfPipeline        = std::exchange(other.m_brdfPipeline, VK_NULL_HANDLE);

    return *this;
}