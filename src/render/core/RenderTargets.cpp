#include "RenderTargets.h"
#include <volk.h>

#include "VulkanContext.h"
#include "../../common/errors.h"

bool RenderTargets::create(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;

    struct Target
    {
        GPUImage         &image;
        VkFormat          format;
        VkImageUsageFlags usage;
        const char       *name;
    };

    const Target targets[]
    {
        { m_depth, DepthFormat,
          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "depth" },
        { m_hdr, HDRFormat,
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "HDR target" },
        { m_selectionMask, SelectionMaskFormat,
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "selection mask" },
        { m_visibility, VisibilityFormat,
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "visibility target" },
    };

    for (const Target &target : targets) {
        if (!m_ctx.createRenderTarget(width, height, target.format, target.usage, target.image)) {
            showError(std::string("RenderTargets: failed to create the ") + target.name);
            destroy();
            return false;
        }
    }
    return true;
}

void RenderTargets::destroy()
{
    if (!m_ctx.device()) {
        return;
    }
    m_ctx.destroyImage(m_depth);
    m_ctx.destroyImage(m_hdr);
    m_ctx.destroyImage(m_selectionMask);
    m_ctx.destroyImage(m_visibility);
    m_width  = 0;
    m_height = 0;
}

bool RenderTargets::recreate(uint32_t width, uint32_t height)
{
    destroy();
    return create(width, height);
}