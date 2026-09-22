#pragma once
#include <vulkan/vulkan_core.h>
#include "gpu_types.h"

class VulkanContext;

class RenderTargets {
public:
    static constexpr VkFormat DepthFormat         = VK_FORMAT_D32_SFLOAT;
    static constexpr VkFormat HDRFormat           = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat SelectionMaskFormat = VK_FORMAT_R8_UNORM;
    static constexpr VkFormat VisibilityFormat    = VK_FORMAT_R32G32_UINT;

    explicit RenderTargets(VulkanContext &ctx) : m_ctx(ctx) {}
    RenderTargets(const RenderTargets &) = delete;
    RenderTargets &operator=(const RenderTargets &) = delete;


    bool create(uint32_t width, uint32_t height);
    void destroy();
    bool recreate(uint32_t width, uint32_t height);
    
    uint32_t width()  const { return m_width; }
    uint32_t height() const { return m_height; }
    VkExtent2D extent() const { return { m_width, m_height }; }


    VkImage     depthImage()              const { return m_depth.image; }
    VkImageView depthImageView()          const { return m_depth.imageView; }
    VkImage     hdrImage()                const { return m_hdr.image; }
    VkImageView hdrImageView()            const { return m_hdr.imageView; }
    VkImage     selectionMaskImage()      const { return m_selectionMask.image; }
    VkImageView selectionMaskImageView()  const { return m_selectionMask.imageView; }
    VkImage     visibilityImage()         const { return m_visibility.image; }
    VkImageView visibilityImageView()     const { return m_visibility.imageView; }


private:
    VulkanContext &m_ctx;

    uint32_t m_width  = 0;
    uint32_t m_height = 0;

    GPUImage m_depth;
    GPUImage m_hdr;
    GPUImage m_selectionMask;
    GPUImage m_visibility;

};
