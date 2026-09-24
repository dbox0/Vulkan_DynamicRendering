#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vector>
#include "ScenePass.h"
#include "../shaders/ShaderProgram.h"


class VulkanContext;


class DepthPrepass {
public:
    explicit DepthPrepass(VulkanContext &ctx) : m_ctx(ctx) {}
    DepthPrepass(const DepthPrepass &) = delete;
    DepthPrepass &operator=(const DepthPrepass &) = delete;

    void appendShaderPrograms(std::vector<ShaderProgram> &out, VkPipelineLayout layout);
    bool createPipelines(VkPipelineLayout layout);
    void destroy();

    void record(VkCommandBuffer cmd, VkBuffer indirectBuffer, const DrawBatches &batches, VkImageView depthView,
                VkExtent2D extent, const VkViewport &viewport) const;

private:
    bool createPipeline(VkPipelineLayout layout, bool masked, VkPipeline &outPipeline);

    VulkanContext &m_ctx;

    VkPipeline     m_pipelineOpaque       = nullptr;
    VkPipeline     m_pipelineMasked       = nullptr;
    VkShaderModule m_opaqueVertexShader   = nullptr;
    VkShaderModule m_maskedVertexShader   = nullptr;
    VkShaderModule m_maskedFragmentShader = nullptr;
};