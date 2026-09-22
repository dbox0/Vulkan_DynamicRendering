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

    struct MipChain {
        VkImage  image      = VK_NULL_HANDLE;
        uint32_t width      = 0;
        uint32_t height     = 0;
        uint32_t mipLevels  = 1;
        uint32_t layerCount = 1;   // 6 for a cubemap: one blit covers every face
        VkPipelineStageFlags2 dstStage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        VkAccessFlags2        dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    };

    // Fills levels 1..n-1 by successive linear blits, then leaves every level
    // SHADER_READ_ONLY_OPTIMAL for dstStage/dstAccess.
    // On entry every level must be TRANSFER_DST_OPTIMAL,
    // level 0's contents must be visible to BLIT stage
    void generateMips(VkCommandBuffer cmd, const MipChain& m);

}