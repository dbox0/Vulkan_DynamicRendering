#include "EnvironmentMap.h"

#include <cstring>
#include <iostream>
#include <ostream>

#include "../../external/stb_image.h"
#include "../../common/errors.h"
#include "../../common/vkbarrier.h"

using namespace render;

bool createCubemap(){}

bool load() {

}

bool EnvironmentMap::loadEquirect(const std::string& path, VkCommandBuffer cmd) {
    int w = 0, h = 0, channels = 0;
    float* data = stbi_loadf(path.c_str(), &w, &h, &channels, 4);
    if (!data) {
        showError("Could not load .hdr equirect image");
        std::cerr << stbi_failure_reason() << std::endl;
        return false;
    }
    const VkDeviceSize bytes = VkDeviceSize(w) * h * 4 * sizeof(float);

    const VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = bytes,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };
    const VmaAllocationCreateInfo bufferAlloc{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
               | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    GPUBuffer staging{};
    VmaAllocationInfo stagingInfo{};
    if (vmaCreateBuffer(m_ctx.allocator, &bufferInfo, &bufferAlloc,
                        &staging.vkBuffer, &staging.allocation, &stagingInfo) != VK_SUCCESS) {
        stbi_image_free(data);
        showError("Could not create equirect staging buffer");
        return false;
    }
    std::memcpy(stagingInfo.pMappedData, data, size_t(bytes));
    stbi_image_free(data);
    m_ctx.uploader->trackBuffer(cmd, staging);

    const VkImageCreateInfo imageInfo{
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = EquidirectFORMAT,
        .extent        = { uint32_t(w), uint32_t(h), 1 },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo imageAlloc{ .usage = VMA_MEMORY_USAGE_AUTO };
    if (vmaCreateImage(m_ctx.allocator, &imageInfo, &imageAlloc,
                       &m_equirect, &m_equirectAlloc, nullptr) != VK_SUCCESS) {
        showError("Could not allocate equirect image");
        return false;
    }

    const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    vkutil::imageBarrier(cmd, {
        .image     = m_equirect,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcStage  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        .dstStage  = VK_PIPELINE_STAGE_2_COPY_BIT,
        .dstAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .range     = range,
    });

    const VkBufferImageCopy2 region{
        .sType            = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent      = imageInfo.extent,
    };
    const VkCopyBufferToImageInfo2 copy{
        .sType          = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2,
        .srcBuffer      = staging.vkBuffer,
        .dstImage       = m_equirect,
        .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .regionCount    = 1,
        .pRegions       = &region,
    };
    vkCmdCopyBufferToImage2(cmd, &copy);

    vkutil::imageBarrier(cmd, {
        .image     = m_equirect,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcStage  = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStage  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .range     = range,
    });

    const VkImageViewCreateInfo viewInfo{
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = m_equirect,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = EquidirectFORMAT,
        .subresourceRange = range,
    };
    return vkCreateImageView(m_ctx.device, &viewInfo, nullptr, &m_equirectView) == VK_SUCCESS;
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
        .viewType         = VK_IMAGE_VIEW_TYPE_CUBE,          // <- not 2D
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

bool EnvironmentMap::createSamplers() {

    const VkSamplerCreateInfo cube{
        .sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter       = VK_FILTER_LINEAR,
        .minFilter       = VK_FILTER_LINEAR,
        .mipmapMode      = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU      = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV      = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW      = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = static_cast<float>(m_settings.generateMips ? VK_LOD_CLAMP_NONE : 0.25f)
    };
    if (vkCreateSampler(m_ctx.device, &cube, nullptr, &m_sampler) != VK_SUCCESS) {
        showError("Could not create cubemap sampler");
        return false;
    }
    const VkSamplerCreateInfo equi{
        .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter    = VK_FILTER_LINEAR,
        .minFilter    = VK_FILTER_LINEAR,
        .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    if (vkCreateSampler(m_ctx.device, &equi, nullptr, &m_equirectSampler) != VK_SUCCESS) {
        showError("Could not create equirect sampler");
        return false;
    }
    return true;
}

