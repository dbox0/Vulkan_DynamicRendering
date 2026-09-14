#pragma once

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>


constexpr uint32_t formatBytesPerPixel(VkFormat format)
{
    switch (format) {
        case VK_FORMAT_R8_UNORM:                            return 1;
        case VK_FORMAT_R8G8_UNORM:                          return 2;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:                       return 4;
        case VK_FORMAT_R16G16B16A16_SFLOAT:                 return 8;
        case VK_FORMAT_R32G32B32A32_SFLOAT:                 return 16;
        default:                                            return 0;   // unsupported
    }
}

struct GPUImage
{
    VkImage image = nullptr;
    VkImageView imageView = nullptr;
    VmaAllocation allocation = nullptr;
};

struct GPUBuffer
{
    VkBuffer vkBuffer = nullptr;
    uint64_t deviceAddress = 0;
    VmaAllocation allocation = nullptr;
};