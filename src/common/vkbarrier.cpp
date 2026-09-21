#include "vkbarrier.h"

#include <algorithm>
#include <vector>

void vkutil::imageBarriers(VkCommandBuffer cmd, std::span<const ImageBarrier> bs) {
    std::vector<VkImageMemoryBarrier2> out;
    out.reserve(bs.size());
    for (const ImageBarrier& b : bs) {
        out.push_back({
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask        = b.srcStage,
            .srcAccessMask       = b.srcAccess,
            .dstStageMask        = b.dstStage,
            .dstAccessMask       = b.dstAccess,
            .oldLayout           = b.oldLayout,
            .newLayout           = b.newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = b.image,
            .subresourceRange    = b.range,
        });
    }
    const VkDependencyInfo dep{
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount  = uint32_t(out.size()),
        .pImageMemoryBarriers     = out.data(),
    };
    vkCmdPipelineBarrier2(cmd, &dep);
}

void vkutil::imageBarrier(VkCommandBuffer cmd, const ImageBarrier& b) {
    imageBarriers(cmd, { &b, 1 });
}

void vkutil::generateMips(VkCommandBuffer cmd, const MipChain& m) {
    int32_t w = int32_t(m.width);
    int32_t h = int32_t(m.height);

    for (uint32_t level = 1; level < m.mipLevels; ++level) {
        imageBarrier(cmd, {
            .image     = m.image,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcStage  = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
            .srcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStage  = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .dstAccess = VK_ACCESS_2_TRANSFER_READ_BIT,
            .range     = { VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 1, 0, m.layerCount },
        });

        const int32_t nw = std::max(w / 2, 1);
        const int32_t nh = std::max(h / 2, 1);

        const VkImageBlit2 blit{
            .sType          = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, m.layerCount },
            .srcOffsets     = { { 0, 0, 0 }, { w, h, 1 } },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, m.layerCount },
            .dstOffsets     = { { 0, 0, 0 }, { nw, nh, 1 } },
        };
        const VkBlitImageInfo2 info{
            .sType          = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
            .srcImage       = m.image,
            .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .dstImage       = m.image,
            .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .regionCount    = 1,
            .pRegions       = &blit,
            .filter         = VK_FILTER_LINEAR,
        };
        vkCmdBlitImage2(cmd, &info);

        w = nw;
        h = nh;
    }

    // Every level but the last was a blit source; the last is still a destination.
    const uint32_t last = m.mipLevels - 1;
    const ImageBarrier toRead[2]{
        {
            .image     = m.image,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcStage  = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .srcAccess = VK_ACCESS_2_NONE,
            .dstStage  = m.dstStage,
            .dstAccess = m.dstAccess,
            .range     = { VK_IMAGE_ASPECT_COLOR_BIT, 0, last, 0, m.layerCount },
        },
        {
            .image     = m.image,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcStage  = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
            .srcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStage  = m.dstStage,
            .dstAccess = m.dstAccess,
            .range     = { VK_IMAGE_ASPECT_COLOR_BIT, last, 1, 0, m.layerCount },
        },
    };
    imageBarriers(cmd, last > 0 ? std::span<const ImageBarrier>(toRead, 2)
                                : std::span<const ImageBarrier>(&toRead[1], 1));
}
