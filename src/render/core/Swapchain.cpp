#include "Swapchain.h"

#include <volk.h>
#include <vk_mem_alloc.h>
#include <algorithm>
#include <vector>

#include "VulkanContext.h"
#include "../../common/errors.h"

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
    if (!createHdrTarget(m_width, m_height)) {
        return false;
    }

    m_needsRecreate = false;
    return true;
}

bool Swapchain::createDepthBuffer(uint32_t width, uint32_t height)
{
    if (!m_ctx.createRenderTarget(width, height, DepthFormat,
                                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, m_depth)) {
        showError("Error creating the depth buffer");
        return false;
    }
    return true;
}

bool Swapchain::createSelectionMask(uint32_t width, uint32_t height)
{
    // Written as an attachment by the mask pass, read as a texture by the
    // composite pass in the same frame.
    if (!m_ctx.createRenderTarget(width, height, SelectionMaskFormat,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                  m_selectionMask)) {
        showError("Error creating the selection mask");
        return false;
    }
    return true;
}

bool Swapchain::createHdrTarget(uint32_t width, uint32_t height) {
    if (!m_ctx.createRenderTarget(width, height, HDRFormat,
                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                   m_hdrTarget)) {
        showError("Error creating the hdr target");
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

    m_ctx.destroyImage(m_depth);
    m_ctx.destroyImage(m_selectionMask);
    m_ctx.destroyImage(m_hdrTarget);
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