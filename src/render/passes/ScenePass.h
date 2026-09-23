#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <array>
#include <vector>
#include "../shaders/ShaderProgram.h"

class VulkanContext;

struct DrawBatch
{
    uint32_t        first     = 0;
    uint32_t        count     = 0;
    VkCullModeFlags cullMode  = VK_CULL_MODE_BACK_BIT;
    bool            alphaMask = false;
    bool            blend     = false;
};

enum class DrawKind : uint32_t { Opaque = 0, Masked = 1, Blended = 2 };

constexpr uint32_t drawBucket(DrawKind kind, bool doubleSided)
{
    return static_cast<uint32_t>(kind) * 2u + (doubleSided ? 1u : 0u);
}

constexpr uint32_t FirstBlendedBucket = drawBucket(DrawKind::Blended, false);

using DrawBatches = std::array<DrawBatch, 6>;

class ScenePass
{
public:
    explicit ScenePass(VulkanContext &ctx) : m_ctx(ctx) {}
    ScenePass(const ScenePass &) = delete;
    ScenePass &operator=(const ScenePass &) = delete;

    void appendShaderPrograms(std::vector<ShaderProgram> &out, VkPipelineLayout layout);
    bool createPipelines(VkPipelineLayout layout);
    void destroy();
    void record(VkCommandBuffer cmd, VkBuffer indirectBuffer, const DrawBatches &batches) const;

private:
    bool createPipeline(VkPipelineLayout layout, bool blendEnabled, VkPipeline &outPipeline);

    VulkanContext &m_ctx;

    VkPipeline     m_pipelineOpaque = nullptr;
    VkPipeline     m_pipelineBlend  = nullptr;
    VkShaderModule m_vertexShader   = nullptr;
    VkShaderModule m_fragmentShader = nullptr;
};
