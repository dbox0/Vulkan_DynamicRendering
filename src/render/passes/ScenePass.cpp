#include "ScenePass.h"

#include <volk.h>
#include <array>

#include "../core/Swapchain.h"
#include "../core/VulkanContext.h"
#include "../../common/errors.h"
#include "../core/RenderTargets.h"

void ScenePass::appendShaderPrograms(std::vector<ShaderProgram> &out, VkPipelineLayout layout)
{
    out.push_back(graphicsProgram("forward/pbr.vert", "forward/pbr.frag", &m_vertexShader, &m_fragmentShader,
                    { &m_pipelineOpaque, &m_pipelineBlend },
                    [this, layout] { return createPipeline(layout, false, m_pipelineOpaque) &&
                                            createPipeline(layout, true,  m_pipelineBlend); }));
}

bool ScenePass::createPipelines(VkPipelineLayout layout)
{
    return createPipeline(layout, false, m_pipelineOpaque) &&
           createPipeline(layout, true,  m_pipelineBlend);
}

void ScenePass::destroy()
{
    const VkDevice device = m_ctx.device();
    if (!device) {
        return;
    }
    for (VkPipeline *p : { &m_pipelineOpaque, &m_pipelineBlend }) {
        if (*p) {
            vkDestroyPipeline(device, *p, nullptr);
            *p = nullptr;
        }
    }
    for (VkShaderModule *m : { &m_vertexShader, &m_fragmentShader }) {
        if (*m) {
            vkDestroyShaderModule(device, *m, nullptr);
            *m = nullptr;
        }
    }
}

void ScenePass::record(VkCommandBuffer cmd, VkBuffer indirectBuffer, const DrawBatches &batches) const
{
    VkPipeline boundPipeline = nullptr;

    for (const DrawBatch &batch : batches) {
        if (batch.count == 0) {
            continue;
        }
        VkPipeline wantedPipeline = batch.blend? m_pipelineBlend : m_pipelineOpaque;

        if (wantedPipeline != boundPipeline) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wantedPipeline);
            boundPipeline = wantedPipeline;
        }
        vkCmdSetCullMode(cmd, batch.cullMode);
        vkCmdDrawIndexedIndirect(
            cmd, indirectBuffer,
            static_cast<VkDeviceSize>(batch.first) * sizeof(VkDrawIndexedIndirectCommand),
            batch.count, sizeof(VkDrawIndexedIndirectCommand));
    }
}

bool ScenePass::createPipeline(VkPipelineLayout layout, bool blendEnabled, VkPipeline &outPipeline)
{
    const char *entryPoint = "main";
    const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages
    {
        VkPipelineShaderStageCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = m_vertexShader,
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

    // Vertex pulling: no vertex input bindings, the shader reads through BDA.
    VkPipelineVertexInputStateCreateInfo vertInputInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };

    // Blended geometry still tests against the opaque depth, but must not
    // write, or the draw order within the transparent bucket stops mattering.
    VkPipelineDepthStencilStateCreateInfo depthStencilInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = blendEnabled ? VK_FALSE : VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_GREATER,   // reverse Z
        .stencilTestEnable = VK_FALSE
    };

    // Set dynamically per frame, but the structs still have to be present.
    VkPipelineViewportStateCreateInfo viewportInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = nullptr,
        .scissorCount = 1,
        .pScissors = nullptr
    };

    // cullMode is dynamic now -- this value is only what the pipeline is
    // created with and is overridden by vkCmdSetCullMode before every batch.
    VkPipelineRasterizationStateCreateInfo rasterInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisampleInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachState
    {
        .blendEnable = blendEnabled ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    VkPipelineColorBlendStateCreateInfo blendInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachState
    };

    const std::array<VkDynamicState, 3> dynamicStates
    {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_CULL_MODE
    };
    VkPipelineDynamicStateCreateInfo dynamicStateInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };

    // Dynamic rendering: no VkRenderPass.
    constexpr VkFormat colorFormat = RenderTargets::HDRFormat;
    VkPipelineRenderingCreateInfo renderInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
        .depthAttachmentFormat = RenderTargets::DepthFormat
    };

    VkGraphicsPipelineCreateInfo pipelineInfo
    {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderInfo,
        .stageCount = static_cast<uint32_t>(shaderStages.size()),
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
        .renderPass = VK_NULL_HANDLE,
    };

    if (vkCreateGraphicsPipelines(m_ctx.device(), nullptr, 1, &pipelineInfo, nullptr, &outPipeline) != VK_SUCCESS) {
        showError("Failed to create the graphics pipeline");
        return false;
    }
    return true;
}
