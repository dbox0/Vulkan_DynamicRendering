#include "EnvironmentMap.h"

#include <cstring>
#include <iostream>
#include <ostream>

#include "../../external/stb_image.h"
#include "../../common/errors.h"

bool createCubemap(){}


bool render::EnvironmentMap::loadEquirect(const std::string& path, VkCommandBuffer cmd) {
    int w = 0, h = 0, channels = 0;

    float* data = stbi_loadf(path.c_str(), &w, &h, &channels, 4);

    if (!data) {
        showError("Could not load .hdr equidirect image");
        std::cerr << stbi_failure_reason() << std::endl;
        return false;
    }
    const VkDeviceSize bytes = VkDeviceSize(w)*h*4*sizeof(float);

    VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bytes,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    };
    VmaAllocationCreateInfo bufferAlloc
    {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    VmaAllocationInfo stagingInfo{};
    if (vmaCreateBuffer(m_bakeContext.allocator, &bufferInfo,&bufferAlloc,
                        &m_stagingBuff,&m_stagingAlloc,&stagingInfo)
                        != VK_SUCCESS)
    {
        stbi_image_free(data);
        showError("Could not create hdr equirect image staging buffer");
        return false;
    }
    std::memcpy(stagingInfo.pMappedData, data, size_t(bytes));
    stbi_image_free(data);

    // Device Image
    VkImageCreateInfo imageInfo{
        .sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format    = EnvMapFORMAT,
        .extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    VmaAllocationCreateInfo imageAlloc{
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    if (vmaCreateImage(m_bakeContext.allocator, &imageInfo,&imageAlloc,
                        &m_equirect,&m_equirectAlloc,nullptr) != VK_SUCCESS)
    {
        showError("Could not allocate hdr env equirect image");
        return false;
    }
    const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    transition(cmd,m_equirect,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,range);

    const VkBufferImageCopy2 region{
        .sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2,
        .bufferOffset      = 0,
        .bufferRowLength   = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel       = 0,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = imageInfo.extent,
    };
    const VkCopyBufferToImageInfo2 copy{
        .sType          = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2,
        .srcBuffer      = m_stagingBuff,
        .dstImage       = m_equirect,
        .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .regionCount    = 1,
        .pRegions       = &region,
    };
    vkCmdCopyBufferToImage2(cmd, &copy);

    transition(cmd,m_equirect,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,range);

    VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = m_equirect,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = imageInfo.format,
        .subresourceRange = range
    };

    return vkCreateImageView(m_bakeContext.device, &viewInfo, nullptr, &m_equirectView);
}

bool render::EnvironmentMap::createCubemap(Cubemap& out, uint32_t size, uint32_t mips) {
    out.size   = size;
    out.mips   = mips;
    out.format = VK_FORMAT_R16G16B16A16_SFLOAT;

    VkImageCreateInfo imgInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imgInfo.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imgInfo.imageType     = VK_IMAGE_TYPE_2D;
    imgInfo.format        = out.format;
    imgInfo.extent        = { size, size, 1 };
    imgInfo.mipLevels     = mips;
    imgInfo.arrayLayers   = 6;
    imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc{.usage = VMA_MEMORY_USAGE_AUTO};

    if (vmaCreateImage(m_bakeContext.allocator, &imgInfo, &alloc,&out.image,&out.allocation,nullptr) != VK_SUCCESS) {
        showError("Could not create cubemap");
        return false;
    }

    VkImageViewCreateInfo cubeView{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = out.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = out.format,
        .subresourceRange { VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 6 }
    };

    if (vkCreateImageView(m_bakeContext.device, &cubeView, nullptr, &out.cubeView) != VK_SUCCESS) {
        showError("Could not create cubemap ImageView");
        return false;
    }
    out.mipStorageViews.resize(mips, VK_NULL_HANDLE);
    for (uint32_t m = 0; m < mips; ++m) {
        VkImageViewCreateInfo sv{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        sv.image            = out.image;
        sv.viewType         = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        sv.format           = out.format;
        sv.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, m, 1, 0, 6 };

        if (vkCreateImageView(m_bakeContext.device, &sv, nullptr, &out.mipStorageViews[m]) != VK_SUCCESS)
            return false;
    }
    return true;
}