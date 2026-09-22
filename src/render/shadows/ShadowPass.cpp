#include "ShadowPass.h"

#include <volk.h>
#include <array>

#include "../core/VulkanContext.h"
#include "../../common/errors.h"

bool ShadowPass::createTarget(uint32_t resolution)
{
    return m_map.create(resolution);
}

void ShadowPass::appendShaderPrograms(std::vector<ShaderProgram> &out, VkPipelineLayout layout)
{
    out.push_back({ "shadow/shadow.vert", "shadow/shadow.frag", &m_vertexShader, &m_fragmentShader,
                    { &m_pipeline }, [this, layout] { return createPipelines(layout); } });
}

void ShadowPass::destroy()
{
    const VkDevice device = m_ctx.device();
    if (!device) {
        return;
    }
    if (m_pipeline) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = nullptr;
    }
    for (VkShaderModule *m : { &m_vertexShader, &m_fragmentShader }) {
        if (*m) {
            vkDestroyShaderModule(device, *m, nullptr);
            *m = nullptr;
        }
    }
    m_map.destroy();
}

bool ShadowPass::createPipelines(VkPipelineLayout layout)
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
        .renderPass = VK_NULL_HANDLE
    };

    if (vkCreateGraphicsPipelines(m_ctx.device(), nullptr, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        showError("Failed to create the shadow pipeline");
        return false;
    }
    return true;
}

void ShadowPass::record(VkCommandBuffer cmd, VkBuffer indirectBuffer, const DrawBatches &batches,
                        const ShadowSettings &settings, bool active) const
{
    VkImageMemoryBarrier2 toAttachment
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .image = m_map.image(),
        .subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 }
    };
    VkDependencyInfo toAttachmentDep
    {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toAttachment
    };
    vkCmdPipelineBarrier2(cmd, &toAttachmentDep);

    VkRenderingAttachmentInfo depthAttachInfo
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = m_map.imageView(),
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
        // Positive height, unlike the scene pass: nothing here is displayed,
        // so the plain mapping is what pbr.frag's uv = ndc * 0.5 + 0.5 expects.
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

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

        // Negated: reverse Z means pushing a caster AWAY from the light is a
        // smaller depth value, so the bias that fixes acne has to go down.
        vkCmdSetDepthBias(cmd, -settings.constantBias, 0.0f, -settings.slopeBias);

        // Disabled clears and transitions: pbr.frag samples the map
        // A cleared map reads as "lit everywhere".
        //
        // Buckets 0 and 1 only -- opaque and alpha-masked, single and double
        // sided. Blended geometry is skipped
        for (uint32_t bucket = 0; active && bucket < 2; ++bucket) {
            const DrawBatch &batch = batches[bucket];
            if (batch.count == 0) {
                continue;
            }
            vkCmdSetCullMode(cmd, batch.cullMode);
            vkCmdDrawIndexedIndirect(
                cmd, indirectBuffer,
                static_cast<VkDeviceSize>(batch.first) * sizeof(VkDrawIndexedIndirectCommand),
                batch.count, sizeof(VkDrawIndexedIndirectCommand));
        }
    }
    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier2 toSampled
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image = m_map.image(),
        .subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 }
    };
    VkDependencyInfo toSampledDep
    {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toSampled
    };
    vkCmdPipelineBarrier2(cmd, &toSampledDep);
}
