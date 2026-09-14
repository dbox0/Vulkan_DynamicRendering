#include "Swapchain.h"

#include <volk.h>
#include <vk_mem_alloc.h>
#include <algorithm>
#include <vector>

#include "VulkanContext.h"
#include "../common/errors.h"

bool Swapchain::create(uint32_t width, uint32_t height)
{
    m_width  = width;
    m_height = height;

    VkSurfaceCapabilitiesKHR surfaceCaps{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_ctx.physical(), m_ctx.surface(), &surfaceCaps) != VK_SUCCESS) {
        showError("Failed to get surface capabilities");
        return false;
    }

    // The surface can dictate an extent; honour it instead of the window size.
    if (surfaceCaps.currentExtent.width != UINT32_MAX) {
        m_width  = surfaceCaps.currentExtent.width;
        m_height = surfaceCaps.currentExtent.height;
    }
    m_width  = std::clamp(m_width,  surfaceCaps.minImageExtent.width,  surfaceCaps.maxImageExtent.width);
    m_height = std::clamp(m_height, surfaceCaps.minImageExtent.height, surfaceCaps.maxImageExtent.height);

    // Moved here from findPhysicalDevice: the format the swapchain wants is
    // the swapchain's concern, not the context's.
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_ctx.physical(), m_ctx.surface(), &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_ctx.physical(), m_ctx.surface(), &formatCount, surfaceFormats.data());

    bool formatSupported = false;
    for (const VkSurfaceFormatKHR &surfFormat : surfaceFormats) {
        if (surfFormat.format == ColorFormat) {
            formatSupported = true;
            break;
        }
    }
    if (!formatSupported) {
        showError("Requested swapchain format is not supported by the surface");
        return false;
    }

    uint32_t requestedImageCount = std::max(2u, surfaceCaps.minImageCount);
    if (surfaceCaps.maxImageCount > 0) {
        requestedImageCount = std::min(requestedImageCount, surfaceCaps.maxImageCount);
    }

    VkSwapchainCreateInfoKHR swapchainCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = m_ctx.surface(),
        .minImageCount = requestedImageCount,
        .imageFormat = ColorFormat,
        .imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = { .width = m_width, .height = m_height },
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = surfaceCaps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR   // guaranteed available; vsync
    };

    if (vkCreateSwapchainKHR(m_ctx.device(), &swapchainCreateInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        showError("Error creating swapchain");
        return false;
    }

    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(m_ctx.device(), m_swapchain, &imageCount, nullptr);
    m_images.resize(imageCount);
    vkGetSwapchainImagesKHR(m_ctx.device(), m_swapchain, &imageCount, m_images.data());

    m_imageViews.resize(imageCount);
    for (size_t i = 0; i < m_images.size(); ++i) {
        VkImageViewCreateInfo imgViewInfo
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = m_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = ColorFormat,
            .subresourceRange
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1
            }
        };
        if (vkCreateImageView(m_ctx.device(), &imgViewInfo, nullptr, &m_imageViews[i]) != VK_SUCCESS) {
            showError("Error creating swapchain image view");
            return false;
        }
    }

    // One render-complete semaphore per swapchain image. These must be
    // per-image, not per-frame-in-flight, because presentation waits on them.
    m_renderCompleteSemaphores.resize(imageCount);
    for (VkSemaphore &semaphore : m_renderCompleteSemaphores) {
        VkSemaphoreCreateInfo semInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        if (vkCreateSemaphore(m_ctx.device(), &semInfo, nullptr, &semaphore) != VK_SUCCESS) {
            showError("Error creating the render-complete semaphore");
            return false;
        }
    }

    if (!createDepthBuffer(m_width, m_height)) {
        return false;
    }
    if (!createSelectionMask(m_width, m_height)) {
        return false;
    }

    m_needsRecreate = false;
    return true;
}

bool Swapchain::createDepthBuffer(uint32_t width, uint32_t height)
{
    VkImageCreateInfo depthCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = DepthFormat,
        .extent{ .width = width, .height = height, .depth = 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    VmaAllocationCreateInfo allocInfo
    {
        .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
    };

    if (vmaCreateImage(m_ctx.allocator(), &depthCreateInfo, &allocInfo,
                       &m_depthImage, &m_depthImageAllocation, nullptr) != VK_SUCCESS)
    {
        showError("Error creating depth buffer image");
        return false;
    }

    VkImageViewCreateInfo depthImgViewInfo
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = m_depthImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = DepthFormat,
        .subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 }
    };

    if (vkCreateImageView(m_ctx.device(), &depthImgViewInfo, nullptr, &m_depthImageView) != VK_SUCCESS) {
        showError("Error creating depth image view");
        return false;
    }
    return true;
}

bool Swapchain::createSelectionMask(uint32_t width, uint32_t height)
{
    VkImageCreateInfo maskCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = SelectionMaskFormat,
        .extent{ .width = width, .height = height, .depth = 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        // Written as an attachment by the mask pass, read as a texture by the
        // composite pass in the same frame.
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    VmaAllocationCreateInfo allocInfo
    {
        .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
    };

    if (vmaCreateImage(m_ctx.allocator(), &maskCreateInfo, &allocInfo,
                       &m_maskImage, &m_maskAllocation, nullptr) != VK_SUCCESS)
    {
        showError("Error creating the selection mask image");
        return false;
    }

    VkImageViewCreateInfo maskViewInfo
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = m_maskImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = SelectionMaskFormat,
        .subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
    };

    if (vkCreateImageView(m_ctx.device(), &maskViewInfo, nullptr, &m_maskImageView) != VK_SUCCESS) {
        showError("Error creating the selection mask image view");
        return false;
    }
    return true;
}

void Swapchain::destroy()
{
    if (!m_ctx.device()) {
        return;
    }

    for (VkImageView view : m_imageViews) {
        vkDestroyImageView(m_ctx.device(), view, nullptr);
    }
    m_imageViews.clear();

    for (VkSemaphore semaphore : m_renderCompleteSemaphores) {
        vkDestroySemaphore(m_ctx.device(), semaphore, nullptr);
    }
    m_renderCompleteSemaphores.clear();

    m_images.clear();

    if (m_swapchain) {
        vkDestroySwapchainKHR(m_ctx.device(), m_swapchain, nullptr);
        m_swapchain = nullptr;
    }

    if (m_depthImageView) {
        vkDestroyImageView(m_ctx.device(), m_depthImageView, nullptr);
        m_depthImageView = nullptr;
    }
    if (m_depthImage) {
        vmaDestroyImage(m_ctx.allocator(), m_depthImage, m_depthImageAllocation);
        m_depthImage = nullptr;
        m_depthImageAllocation = nullptr;
    }

    if (m_maskImageView) {
        vkDestroyImageView(m_ctx.device(), m_maskImageView, nullptr);
        m_maskImageView = nullptr;
    }
    if (m_maskImage) {
        vmaDestroyImage(m_ctx.allocator(), m_maskImage, m_maskAllocation);
        m_maskImage = nullptr;
        m_maskAllocation = nullptr;
    }
}

bool Swapchain::recreate(uint32_t width, uint32_t height)
{
    vkDeviceWaitIdle(m_ctx.device());
    destroy();
    return create(width, height);
}

bool Swapchain::acquireNextImage(VkSemaphore imageAcquiredSemaphore, uint32_t &outImageIndex)
{
    const VkResult result = vkAcquireNextImageKHR(m_ctx.device(), m_swapchain, UINT64_MAX,
                                                  imageAcquiredSemaphore, VK_NULL_HANDLE,
                                                  &outImageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        m_needsRecreate = true;
        return false;                 // skip this frame entirely
    }
    if (result == VK_SUBOPTIMAL_KHR) {
        m_needsRecreate = true;
        return true;                  // this frame is still presentable
    }
    return result == VK_SUCCESS;
}