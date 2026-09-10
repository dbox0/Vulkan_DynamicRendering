#pragma once

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>

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