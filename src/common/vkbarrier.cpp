#include "vkbarrier.h"

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