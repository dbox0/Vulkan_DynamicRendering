#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vector>
#include "../passes/ScenePass.h"        // DrawBatches
#include "ShadowMap.h"
#include "../shaders/ShaderProgram.h"

class VulkanContext;

// Sun shadow: the shadow map target plus the depth-only pipeline that fills it.
//
// The pipeline shares the renderer's scene layout -- same push constants,
// RenderItem buffer, and bindless set for the alpha-mask lookup.
class ShadowPass
{
public:
    explicit ShadowPass(VulkanContext &ctx) : m_ctx(ctx), m_map(ctx) {}
    ShadowPass(const ShadowPass &) = delete;
    ShadowPass &operator=(const ShadowPass &) = delete;

    // Separate from createPipelines: the scene layout takes the map's
    // descriptor set layout as set 1, so the map has to exist first.
    bool createTarget(uint32_t resolution);

    void appendShaderPrograms(std::vector<ShaderProgram> &out, VkPipelineLayout layout);
    bool createPipelines(VkPipelineLayout layout);
    void destroy();

    // Depth-only pass from the sun's point of view, into the shadow map.
    // Opaque and alpha-masked buckets only. Runs even when inactive, so the
    // map is cleared ("lit everywhere") and ends up SHADER_READ_ONLY_OPTIMAL.
    void record(VkCommandBuffer cmd, VkBuffer indirectBuffer, const DrawBatches &batches,
                const ShadowSettings &settings, bool active) const;

    const ShadowMap &map() const { return m_map; }

private:
    // Opaque casters get no frag stage.
    // Masked casters run the alpha Test
    bool createPipeline(VkPipelineLayout layout, bool masked, VkPipeline &outPipeline);

    VulkanContext &m_ctx;
    ShadowMap      m_map;

    VkPipeline     m_pipelineOpaque     = nullptr;
    VkPipeline     m_pipelineMasked     = nullptr;
    VkShaderModule m_opaqueVertexShader = nullptr;
    VkShaderModule m_maskedVertexShader = nullptr;
    VkShaderModule m_fragmentShader     = nullptr;
};
