#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <array>
#include <vector>
#include "../shaders/ShaderProgram.h"

class VulkanContext;

struct DrawBatch
{
    uint32_t        first    = 0;
    uint32_t        count    = 0;
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    bool            blend    = false;
};

// Bucket 0/1 = opaque single/double sided, 2/3 = blended single/double.
// Each bucket is one contiguous run in the frame's indirect buffer.
using DrawBatches = std::array<DrawBatch, 4>;

// The lit geometry: opaque and blended PBR pipelines, drawn batch by batch
// out of the frame's indirect buffer.
class ScenePass
{
public:
    explicit ScenePass(VulkanContext &ctx) : m_ctx(ctx) {}
    ScenePass(const ScenePass &) = delete;
    ScenePass &operator=(const ScenePass &) = delete;

    void appendShaderPrograms(std::vector<ShaderProgram> &out, VkPipelineLayout layout);
    bool createPipelines(VkPipelineLayout layout);
    void destroy();

    // Inside the caller's rendering scope. Viewport, scissor, index buffer,
    // descriptor sets and push constants of the shared layout are already set.
    void record(VkCommandBuffer cmd, VkBuffer indirectBuffer, const DrawBatches &batches) const;

private:
    bool createPipeline(VkPipelineLayout layout, bool blendEnabled, VkPipeline &outPipeline);

    VulkanContext &m_ctx;

    VkPipeline     m_pipelineOpaque = nullptr;
    VkPipeline     m_pipelineBlend  = nullptr;
    VkShaderModule m_vertexShader   = nullptr;
    VkShaderModule m_fragmentShader = nullptr;
};
