#pragma once
#include <volk.h>
#include <span>

namespace vkutil {

    struct ImageBarrier {
        VkImage       image     = VK_NULL_HANDLE;
        VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkPipelineStageFlags2 srcStage  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkAccessFlags2        srcAccess = 0;
        VkPipelineStageFlags2 dstStage  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkAccessFlags2        dstAccess = 0;
        VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0,
                                       VK_REMAINING_MIP_LEVELS, 0,
                                       VK_REMAINING_ARRAY_LAYERS };
    };

    void imageBarrier(VkCommandBuffer cmd, const ImageBarrier& b);
    void imageBarriers(VkCommandBuffer cmd, std::span<const ImageBarrier> bs);

}