#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <vk_mem_alloc.h>
#include "../../common/gpu_types.h"

class VulkanContext;

// Swapchain images + views, the per-image render-complete semaphores, and the
// depth buffer -- all of which are recreated together on resize.
class Swapchain
{
public:
    static constexpr VkFormat ColorFormat = VK_FORMAT_B8G8R8A8_SRGB;
    static constexpr VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;
    static constexpr VkFormat HDRFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    // Coverage of the editor's selection, one byte per pixel. Lives here
    // rather than in Renderer because it is screen-sized: it has to be
    // rebuilt on resize alongside the depth buffer, and there is exactly one
    // place that already knows how to do that.
    static constexpr VkFormat SelectionMaskFormat = VK_FORMAT_R8_UNORM;

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
    VkImage        depthImage()             const { return m_depth.image; }
    VkImageView    depthImageView()         const { return m_depth.imageView; }
    VkImage        selectionMaskImage()     const { return m_selectionMask.image; }
    VkImageView    selectionMaskImageView() const { return m_selectionMask.imageView; }
    VkImage        hdrImage()               const { return m_hdrTarget.image; }
    VkImageView    hdrImageView()           const { return m_hdrTarget.imageView; }

    // Acquire wraps the OUT_OF_DATE / SUBOPTIMAL handling so the renderer
    // doesn't have to. Returns false when the caller should skip the frame.
    bool acquireNextImage(VkSemaphore imageAcquiredSemaphore, uint32_t &outImageIndex);
    bool needsRecreate() const { return m_needsRecreate; }
    void flagForRecreate()     { m_needsRecreate = true; }

    uint32_t imageCount() const { return static_cast<uint32_t>(m_images.size()); }

private:
    bool createDepthBuffer(uint32_t width, uint32_t height);
    bool createSelectionMask(uint32_t width, uint32_t height);
    bool createHdrTarget(uint32_t width, uint32_t height);
    VulkanContext &m_ctx;

    VkSwapchainKHR           m_swapchain = nullptr;
    std::vector<VkImage>     m_images;
    std::vector<VkImageView> m_imageViews;
    std::vector<VkSemaphore> m_renderCompleteSemaphores;
    uint32_t m_width  = 0;
    uint32_t m_height = 0;
    bool     m_needsRecreate = false;

    // Screen-sized render targets, rebuilt with the swapchain.
    GPUImage m_depth;
    GPUImage m_selectionMask;
    GPUImage m_hdrTarget;
};