#include "ShadowPass.h"

#include <volk.h>
#include <array>

#include "../core/VulkanContext.h"
#include "../../common/errors.h"
#include "../core/vkbarrier.h"

bool ShadowPass::createTarget(uint32_t resolution)
{
    return m_map.create(resolution);
}

void ShadowPass::appendShaderPrograms(std::vector<ShaderProgram> &out, VkPipelineLayout layout)
{
    // One program per pipeline: a reload nulls exactly the pipelines listed, so
    // a build lambda that also rebuilt the other one would leak it.
    out.push_back(vertexProgram("shadow/shadow_opaque.vert", &m_opaqueVertexShader,
                    { &m_pipelineOpaque },
                    [this, layout] { return createPipeline(layout, false, m_pipelineOpaque); }));
    out.push_back(graphicsProgram("shadow/shadow.vert", "shadow/shadow.frag",
                    &m_maskedVertexShader, &m_fragmentShader,
                    { &m_pipelineMasked },
                    [this, layout] { return createPipeline(layout, true, m_pipelineMasked); }));
}

bool ShadowPass::createPipelines(VkPipelineLayout layout)
{
    return createPipeline(layout, false, m_pipelineOpaque) &&
           createPipeline(layout, true,  m_pipelineMasked);
}
void ShadowPass::destroy()
{
    const VkDevice device = m_ctx.device();
    if (!device) {
        return;
    }
    for (VkPipeline *p : { &m_pipelineOpaque, &m_pipelineMasked }) {
        if (*p) {
            vkDestroyPipeline(device, *p, nullptr);
            *p = nullptr;
        }
    }
    for (VkShaderModule *m : { &m_opaqueVertexShader, &m_maskedVertexShader, &m_fragmentShader }) {
        if (*m) {
            vkDestroyShaderModule(device, *m, nullptr);
            *m = nullptr;
        }
    }
    m_map.destroy();
}

bool ShadowPass::createPipeline(VkPipelineLayout layout, bool masked, VkPipeline &outPipeline)
{
    const char *entryPoint = "main";
    const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages
    {
        VkPipelineShaderStageCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = masked ? m_maskedVertexShader : m_opaqueVertexShader,
            .pName = entryPoint
        },
        VkPipelineShaderStageCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = m_fragmentShader,
            .pName = entryPoint
        }
    };

    VkPipelineVertexInputStateCreateInfo vertInputInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };
    VkPipelineViewportStateCreateInfo viewportInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1
    };

    // depthClamp instead of clipping: a caster in front of the light's near
    // plane gets flattened onto it rather than vanishing
    //  -> what a shadow map wants. depthBias is the other half of the acne fix
    // the normal offset in pbr.frag handles what slope bias cant
    VkPipelineRasterizationStateCreateInfo rasterInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_TRUE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_TRUE,
        .lineWidth = 1.0f
    };
    VkPipelineMultisampleStateCreateInfo multisampleInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };
    VkPipelineDepthStencilStateCreateInfo depthStencilInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_GREATER   // reverse Z, same as the scene
    };

    // No colour attachment at all.
    VkPipelineColorBlendStateCreateInfo blendInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 0
    };

    const std::array<VkDynamicState, 4> dynamicStates
    {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_DEPTH_BIAS
    };
    VkPipelineDynamicStateCreateInfo dynamicStateInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };

    VkPipelineRenderingCreateInfo renderInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 0,
        .depthAttachmentFormat = ShadowMap::Format
    };

    VkGraphicsPipelineCreateInfo pipelineInfo
    {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderInfo,
        .stageCount = masked ? 2u : 1u,
        .pStages = shaderStages.data(),
        .pVertexInputState = &vertInputInfo,
        .pInputAssemblyState = &inputAssemblyInfo,
        .pViewportState = &viewportInfo,
        .pRasterizationState = &rasterInfo,
        .pMultisampleState = &multisampleInfo,
        .pDepthStencilState = &depthStencilInfo,
        .pColorBlendState = &blendInfo,
        .pDynamicState = &dynamicStateInfo,
        .layout = layout,
        .renderPass = VK_NULL_HANDLE
    };

    if (vkCreateGraphicsPipelines(m_ctx.device(), nullptr, 1, &pipelineInfo, nullptr, &outPipeline) != VK_SUCCESS) {
        showError("Failed to create the shadow pipeline");
        return false;
    }
    return true;
}

void ShadowPass::recordCascade(VkCommandBuffer cmd, VkPipelineLayout layout, VkBuffer indirectBuffer,
                               uint32_t cascade, const DrawBatches &batches,
                               const ShadowSettings &settings) const
{
    VkRenderingAttachmentInfo depthAttachInfo
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = m_map.layerView(cascade),
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue{ .depthStencil{ 0.0f, 0 } }
    };
    const VkExtent2D extent{ m_map.resolution(), m_map.resolution() };
    VkRenderingInfo renderingInfo
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea{ .offset{ 0, 0 }, .extent = extent },
        .layerCount = 1,
        .colorAttachmentCount = 0,
        .pDepthAttachment = &depthAttachInfo
    };

    vkCmdBeginRendering(cmd, &renderingInfo);
    {
        VkViewport viewport
        {
            .x = 0.0f,
            .y = 0.0f,
            .width  = static_cast<float>(extent.width),
            .height = static_cast<float>(extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{ .offset{ 0, 0 }, .extent = extent };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // Negated: reverse Z means pushing a caster AWAY from the light
        // bias that fixes acne has to go down.
        vkCmdSetDepthBias(cmd, -settings.constantBias, 0.0f, -settings.slopeBias);

        // Disabled clears and transitions: pbr.frag samples the map
        // A cleared map reads as "lit everywhere".

        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           offsetof(PushConstants, viewIndex), sizeof(uint32_t), &cascade);


        VkPipeline bound = nullptr;
        for (const DrawBatch &batch : batches) {
            if (!settings.enabled || batch.blend || batch.count == 0) {
                continue;
            }
            const VkPipeline wanted = batch.alphaMask ? m_pipelineMasked : m_pipelineOpaque;
            if (wanted != bound) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wanted);
                bound = wanted;
            }
            vkCmdSetCullMode(cmd, batch.cullMode);
            vkCmdDrawIndexedIndirect(
                cmd, indirectBuffer,
                static_cast<VkDeviceSize>(batch.first) * sizeof(VkDrawIndexedIndirectCommand),
                batch.count, sizeof(VkDrawIndexedIndirectCommand));
        }
    }
    vkCmdEndRendering(cmd);
}

void ShadowPass::beginCascades(VkCommandBuffer cmd) const
{
    vkutil::imageBarrier(cmd, {
        .image     = m_map.image(),
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .srcStage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .srcAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .dstStage  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                     VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .dstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .range     = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, VK_REMAINING_ARRAY_LAYERS },
    });
}

void ShadowPass::endCascades(VkCommandBuffer cmd) const
{
    vkutil::imageBarrier(cmd, {
        .image     = m_map.image(),
        .oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcStage  = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .srcAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstStage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .range     = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, VK_REMAINING_ARRAY_LAYERS },
    });
}