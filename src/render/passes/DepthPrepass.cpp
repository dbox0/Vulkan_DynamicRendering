#include "DepthPrepass.h"
#include <volk.h>
#include <array>

#include "../core/RenderTargets.h"
#include "../core/VulkanContext.h"
#include "../../common/errors.h"


void DepthPrepass::appendShaderPrograms(std::vector<ShaderProgram> &out, VkPipelineLayout layout) {

    out.push_back(vertexProgram("prepass/depth.vert", &m_opaqueVertexShader,
                    { &m_pipelineOpaque },
                    [this, layout] { return createPipeline(layout, false, m_pipelineOpaque); }));
    out.push_back(graphicsProgram("prepass/depth_masked.vert", "prepass/depth_masked.frag",
                    &m_maskedVertexShader, &m_maskedFragmentShader,
                    { &m_pipelineMasked },
                    [this, layout] { return createPipeline(layout, true, m_pipelineMasked); }));
}

bool DepthPrepass::createPipelines(VkPipelineLayout layout) {
    return createPipeline(layout,false,m_pipelineOpaque)
        && createPipeline(layout,true,m_pipelineMasked);
}
void DepthPrepass::destroy() {
    const VkDevice device = m_ctx.device();
    if (!device) {
        return;
    }
    for (VkPipeline *p : {&m_pipelineOpaque, &m_pipelineMasked}) {
        vkDestroyPipeline(device, *p, nullptr);
        *p = nullptr;
    }
    for (VkShaderModule *m : {&m_maskedVertexShader, &m_opaqueVertexShader, &m_maskedFragmentShader}) {
        vkDestroyShaderModule(device, *m, nullptr);
        *m = nullptr;
    }
}

bool DepthPrepass::createPipeline(VkPipelineLayout layout, bool masked, VkPipeline &outPipeline) {
    const std::array<VkPipelineShaderStageCreateInfo, 2> stages
    {
      VkPipelineShaderStageCreateInfo {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT,
          .module = masked? m_maskedVertexShader : m_opaqueVertexShader,
          .pName = "main"
      },
        VkPipelineShaderStageCreateInfo {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = m_maskedFragmentShader,
          .pName = "main"
      }
    };

    const VkPipelineVertexInputStateCreateInfo vertexInput
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    const VkPipelineInputAssemblyStateCreateInfo inputAssembly
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkPipelineViewportStateCreateInfo viewportState
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo raster
    {
        .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode    = VK_CULL_MODE_BACK_BIT,
        .frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth   = 1.0f
    };
     const VkPipelineMultisampleStateCreateInfo multisample
    {
        .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };
    const VkPipelineDepthStencilStateCreateInfo depthStencil
    {
        .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable  = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp   = VK_COMPARE_OP_GREATER   // reverse Z
    };
    const VkPipelineColorBlendStateCreateInfo blend
    {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 0
    };
    const std::array<VkDynamicState, 3> dynamicStates
    {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_CULL_MODE
    };
    const VkPipelineDynamicStateCreateInfo dynamicState
    {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates    = dynamicStates.data()
    };
    const VkPipelineRenderingCreateInfo rendering
    {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount  = 0,
        .depthAttachmentFormat = RenderTargets::DepthFormat
    };
    const VkGraphicsPipelineCreateInfo info
    {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext               = &rendering,
        .stageCount          = masked ? 2u : 1u,
        .pStages             = stages.data(),
        .pVertexInputState   = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState      = &viewportState,
        .pRasterizationState = &raster,
        .pMultisampleState   = &multisample,
        .pDepthStencilState  = &depthStencil,
        .pColorBlendState    = &blend,
        .pDynamicState       = &dynamicState,
        .layout              = layout
    };
    if (vkCreateGraphicsPipelines(m_ctx.device(), nullptr, 1, &info, nullptr, &outPipeline) != VK_SUCCESS) {
        showError("Failed to create a depth prepass pipeline");
        return false;
    }
    return true;
}
void DepthPrepass::record(VkCommandBuffer cmd, VkBuffer indirectBuffer, const DrawBatches &batches,
                          VkImageView depthView, VkExtent2D extent, const VkViewport &viewport) const {

    const VkRenderingAttachmentInfo depthAttach
    {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = depthView,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue{ .depthStencil{ 0.0f, 0 } }     // reverse Z
    };
    const VkRenderingInfo renderingInfo
    {
        .sType            = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea{ .offset{ 0, 0 }, .extent = extent },
        .layerCount       = 1,
        .pDepthAttachment = &depthAttach
    };

    vkCmdBeginRendering(cmd, &renderingInfo);
    {
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        const VkRect2D scissor{ .offset{ 0, 0 }, .extent = extent };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        VkPipeline bound = nullptr;
        for (const DrawBatch &batch : batches) {
            if (batch.blend || batch.count == 0) {
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