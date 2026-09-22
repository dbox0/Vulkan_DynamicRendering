#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <vk_mem_alloc.h>
#include "gpu_types.h"

class VulkanContext;

// Swapchain images + views, the per-image render-complete semaphores, and the
// depth buffer -- all of which are recreated together on resize.
class Swapchain
{
public:
    static constexpr VkFormat ColorFormat = VK_FORMAT_B8G8R8A8_SRGB;

    explicit Swapchain(VulkanContext &ctx) : m_ctx(ctx) {}
    Swapchain(const Swapchain &) = delete;
    Swapchain &operator=(const Swapchain &) = delete;

    bool create(uint32_t width, uint32_t height);
    void destroy();
    bool recreate(uint32_t width, uint32_t height);   // waits idle, destroy + create

    VkSwapchainKHR handle()                 const { return m_swapchain; }
    VkSemaphore    renderCompleteSemaphore(uint32_t i) const { return m_renderCompleteSemaphores[i]; }

    uint32_t       width()                  const { return m_width; }
    uint32_t       height()                 const { return m_height; }

    VkImage        image(uint32_t i)        const { return m_images[i]; }
    VkImageView    imageView(uint32_t i)    const { return m_imageViews[i]; }


    // Acquire wraps the OUT_OF_DATE / SUBOPTIMAL handling so the renderer
    // doesn't have to. Returns false when the caller should skip the frame.
    bool acquireNextImage(VkSemaphore imageAcquiredSemaphore, uint32_t &outImageIndex);
    bool needsRecreate() const { return m_needsRecreate; }
    void flagForRecreate()     { m_needsRecreate = true; }

    uint32_t imageCount() const { return static_cast<uint32_t>(m_images.size()); }

private:
    VulkanContext &m_ctx;

    VkSwapchainKHR           m_swapchain = nullptr;
    std::vector<VkImage>     m_images;
    std::vector<VkImageView> m_imageViews;
    std::vector<VkSemaphore> m_renderCompleteSemaphores;
    uint32_t m_width  = 0;
    uint32_t m_height = 0;
    bool     m_needsRecreate = false;
};