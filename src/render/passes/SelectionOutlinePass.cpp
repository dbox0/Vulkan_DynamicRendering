#include "SelectionOutlinePass.h"

#include <volk.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "../core/Swapchain.h"
#include "../core/VulkanContext.h"
#include "../../assets/Mesh.h"
#include "../../common/errors.h"
#include "../core/RenderTargets.h"

bool SelectionOutlinePass::createResources()
{
    // texelFetch ignores filtering and addressing entirely, but a combined
    // image sampler still needs a sampler object to exist.
    VkSamplerCreateInfo samplerInfo
    {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
    };
    if (vkCreateSampler(m_ctx.device(), &samplerInfo, nullptr, &m_maskSampler) != VK_SUCCESS) {
        showError("Failed to create the selection mask sampler");
        return false;
    }

    VkDescriptorSetLayoutBinding binding
    {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding
    };
    if (vkCreateDescriptorSetLayout(m_ctx.device(), &layoutInfo, nullptr, &m_outlineSetLayout) != VK_SUCCESS) {
        showError("Failed to create the outline descriptor set layout");
        return false;
    }

    VkDescriptorPoolSize poolSize
    {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1
    };
    VkDescriptorPoolCreateInfo poolInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize
    };
    if (vkCreateDescriptorPool(m_ctx.device(), &poolInfo, nullptr, &m_outlinePool) != VK_SUCCESS) {
        showError("Failed to create the outline descriptor pool");
        return false;
    }

    VkDescriptorSetAllocateInfo allocInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_outlinePool,
        .descriptorSetCount = 1,
        .pSetLayouts = &m_outlineSetLayout
    };
    if (vkAllocateDescriptorSets(m_ctx.device(), &allocInfo, &m_outlineSet) != VK_SUCCESS) {
        showError("Failed to allocate the outline descriptor set");
        return false;
    }

    VkPushConstantRange pushConstantRange
    {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(OutlineConstants)
    };
    VkPipelineLayoutCreateInfo pipelineLayoutInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &m_outlineSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange
    };
    if (vkCreatePipelineLayout(m_ctx.device(), &pipelineLayoutInfo, nullptr, &m_outlineLayout) != VK_SUCCESS) {
        showError("Failed to create the outline pipeline layout");
        return false;
    }
    return true;
}

void SelectionOutlinePass::appendShaderPrograms(std::vector<ShaderProgram> &out, VkPipelineLayout sceneLayout)
{
    out.push_back(graphicsProgram("editor/selection_mask.vert", "editor/selection_mask.frag", &m_maskVertexShader, &m_maskFragmentShader,
                    { &m_pipelineMask }, [this, sceneLayout] { return createMaskPipeline(sceneLayout); }));
    out.push_back(graphicsProgram("editor/outline.vert", "editor/outline.frag", &m_outlineVertexShader, &m_outlineFragmentShader,
                    { &m_pipelineOutline }, [this] { return createCompositePipeline(); }));
}

bool SelectionOutlinePass::createPipelines(VkPipelineLayout sceneLayout)
{
    return createMaskPipeline(sceneLayout) && createCompositePipeline();
}

void SelectionOutlinePass::destroy()
{
    const VkDevice device = m_ctx.device();
    if (!device) {
        return;
    }
    for (VkPipeline *p : { &m_pipelineMask, &m_pipelineOutline }) {
        if (*p) {
            vkDestroyPipeline(device, *p, nullptr);
            *p = nullptr;
        }
    }
    if (m_outlineLayout) {
        vkDestroyPipelineLayout(device, m_outlineLayout, nullptr);
        m_outlineLayout = nullptr;
    }
    for (VkShaderModule *m : { &m_maskVertexShader, &m_maskFragmentShader,
                               &m_outlineVertexShader, &m_outlineFragmentShader }) {
        if (*m) {
            vkDestroyShaderModule(device, *m, nullptr);
            *m = nullptr;
        }
    }
    // Frees m_outlineSet with it.
    if (m_outlinePool) {
        vkDestroyDescriptorPool(device, m_outlinePool, nullptr);
        m_outlinePool = nullptr;
        m_outlineSet  = nullptr;
    }
    if (m_outlineSetLayout) {
        vkDestroyDescriptorSetLayout(device, m_outlineSetLayout, nullptr);
        m_outlineSetLayout = nullptr;
    }
    if (m_maskSampler) {
        vkDestroySampler(device, m_maskSampler, nullptr);
        m_maskSampler = nullptr;
    }
}

void SelectionOutlinePass::setMaskView(VkImageView maskView)
{
    if (!m_outlineSet || !maskView) {
        return;
    }

    // Safe to rewrite in place: this is only ever called at startup or
    // straight after Swapchain::recreate(), which waits idle first.
    VkDescriptorImageInfo imageInfo
    {
        .sampler = m_maskSampler,
        .imageView = maskView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    VkWriteDescriptorSet write
    {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_outlineSet,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &imageInfo
    };
    vkUpdateDescriptorSets(m_ctx.device(), 1, &write, 0, nullptr);
}

// ============================================================================
// per frame
// ============================================================================

void SelectionOutlinePass::beginFrame()
{
    m_selectedSlots.clear();
    m_selectionBoundsValid = true;
    m_selectionMin = glm::vec2( std::numeric_limits<float>::max());
    m_selectionMax = glm::vec2(-std::numeric_limits<float>::max());
}

void SelectionOutlinePass::addSelectedDraw(uint32_t slot, const glm::mat4 &viewProj,
                                           const glm::mat4 &worldMatrix, const SubMesh &subMesh)
{
    m_selectedSlots.push_back(slot);

    if (!m_selectionBoundsValid) {
        return;
    }

    const glm::mat4 mvp = viewProj * worldMatrix;

    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 local
        {
            (corner & 1) ? subMesh.boundsMax.x : subMesh.boundsMin.x,
            (corner & 2) ? subMesh.boundsMax.y : subMesh.boundsMin.y,
            (corner & 4) ? subMesh.boundsMax.z : subMesh.boundsMin.z
        };

        const glm::vec4 clip = mvp * glm::vec4(local, 1.0f);
        if (clip.w <= 1e-4f) {
            // A corner behind the eye makes the perspective divide meaningless
            // and the projected box wraps around. Rather than clip the box
            // properly, give up and scissor the whole screen: this is an
            // optimisation, and an optimisation that can be wrong is a bug.
            m_selectionBoundsValid = false;
            return;
        }

        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        m_selectionMin = glm::min(m_selectionMin, ndc);
        m_selectionMax = glm::max(m_selectionMax, ndc);
    }
}

VkRect2D SelectionOutlinePass::selectionScissor(VkExtent2D extent) const
{
    const auto width  = static_cast<int32_t>(extent.width);
    const auto height = static_cast<int32_t>(extent.height);

    const VkRect2D fullScreen
    {
        .offset{ 0, 0 },
        .extent = extent
    };

    if (!m_selectionBoundsValid || m_selectionMin.x > m_selectionMax.x) {
        return fullScreen;
    }

    // NDC -> framebuffer pixels. Y is inverted because the scene viewport has
    // a negative height, so +1 in NDC is the top row.
    const float x0 = (m_selectionMin.x * 0.5f + 0.5f) * static_cast<float>(width);
    const float x1 = (m_selectionMax.x * 0.5f + 0.5f) * static_cast<float>(width);
    const float y0 = (0.5f - m_selectionMax.y * 0.5f) * static_cast<float>(height);
    const float y1 = (0.5f - m_selectionMin.y * 0.5f) * static_cast<float>(height);

    // The dilate reaches `thickness` pixels outside the silhouette, and the
    // box is only an AABB of an AABB, so pad generously.
    const auto pad = static_cast<float>(m_settings.thickness) + 2.0f;

    const int32_t left   = std::max(0,      static_cast<int32_t>(std::floor(x0 - pad)));
    const int32_t top    = std::max(0,      static_cast<int32_t>(std::floor(y0 - pad)));
    const int32_t right  = std::min(width,  static_cast<int32_t>(std::ceil (x1 + pad)));
    const int32_t bottom = std::min(height, static_cast<int32_t>(std::ceil (y1 + pad)));

    if (right <= left || bottom <= top) {
        return VkRect2D{ .offset{ 0, 0 }, .extent{ 0, 0 } };   // fully offscreen
    }

    return VkRect2D
    {
        .offset{ left, top },
        .extent{ static_cast<uint32_t>(right - left), static_cast<uint32_t>(bottom - top) }
    };
}

void SelectionOutlinePass::recordMask(VkCommandBuffer cmd, VkBuffer indirectBuffer,
                                      VkImage maskImage, VkImageView maskView, VkExtent2D extent) const
{
    // UNDEFINED as the old layout: the mask is fully cleared and rewritten
    // every frame it is used, so its previous contents are worthless. The src
    // scope still has to name the fragment stage -- one image serves all
    // frames in flight, and the previous frame's composite may still be
    // sampling it.
    VkImageMemoryBarrier2 toAttachment
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .image = maskImage,
        .subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
    };
    VkDependencyInfo toAttachmentDep
    {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toAttachment
    };
    vkCmdPipelineBarrier2(cmd, &toAttachmentDep);

    VkRenderingAttachmentInfo maskAttachInfo
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = maskView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue{ .color{ 0.0f, 0.0f, 0.0f, 0.0f } }
    };

    // renderArea is the whole image on purpose, even though the geometry only
    // covers a corner of it: loadOp only clears inside renderArea, and the
    // composite's dilate reads a few pixels beyond the silhouette. Leaving
    // last frame's bytes out there would paint phantom outlines.
    VkRenderingInfo maskRenderingInfo
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea{ .offset{ 0, 0 }, .extent = extent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &maskAttachInfo
    };

    vkCmdBeginRendering(cmd, &maskRenderingInfo);
    {
        // Identical to the scene viewport, flip included, or the mask lands
        // upside down relative to the image it is meant to outline.
        VkViewport viewport
        {
            .x = 0,
            .y = static_cast<float>(extent.height),
            .width  =  static_cast<float>(extent.width),
            .height = -static_cast<float>(extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{ .offset{ 0, 0 }, .extent = extent };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineMask);

        // One indirect draw per selected slot. The sort in writeDrawCommands
        // scatters a node's submeshes across buckets, so they are not
        // contiguous -- but a selection is a handful of draws, not thousands.
        for (const uint32_t slot : m_selectedSlots) {
            vkCmdDrawIndexedIndirect(
                cmd, indirectBuffer,
                static_cast<VkDeviceSize>(slot) * sizeof(VkDrawIndexedIndirectCommand),
                1, sizeof(VkDrawIndexedIndirectCommand));
        }
    }
    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier2 toSampled
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image = maskImage,
        .subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
    };
    VkDependencyInfo toSampledDep
    {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toSampled
    };
    vkCmdPipelineBarrier2(cmd, &toSampledDep);
}

void SelectionOutlinePass::recordComposite(VkCommandBuffer cmd, VkExtent2D extent) const
{
    const VkRect2D outlineScissor = selectionScissor(extent);
    if (outlineScissor.extent.width != 0 && outlineScissor.extent.height != 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineOutline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_outlineLayout, 0, 1, &m_outlineSet, 0, nullptr);
        vkCmdPushConstants(cmd, m_outlineLayout,
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(OutlineConstants), &m_settings);

        // A fullscreen triangle clipped to the selection's screen box.
        // Without the scissor this would run the dilate over every
        // pixel on screen to light up a few hundred of them.
        vkCmdSetScissor(cmd, 0, 1, &outlineScissor);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
}

// ============================================================================
// pipelines
// ============================================================================

bool SelectionOutlinePass::createMaskPipeline(VkPipelineLayout sceneLayout)
{
    const char *entryPoint = "main";
    const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages
    {
        VkPipelineShaderStageCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = m_maskVertexShader,
            .pName = entryPoint
        },
        VkPipelineShaderStageCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = m_maskFragmentShader,
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

    VkPipelineRasterizationStateCreateInfo rasterInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
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
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachState
    {
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT
    };
    VkPipelineColorBlendStateCreateInfo blendInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachState
    };

    const std::array<VkDynamicState, 2> dynamicStates
    {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicStateInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };

    constexpr VkFormat maskFormat = RenderTargets::SelectionMaskFormat;
    VkPipelineRenderingCreateInfo renderInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &maskFormat,
        .depthAttachmentFormat = VK_FORMAT_UNDEFINED
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
        // Shares the scene layout, so the descriptor set and FrameConstants
        // already bound on the command buffer apply unchanged.
        .layout = sceneLayout,
        .renderPass = VK_NULL_HANDLE
    };

    if (vkCreateGraphicsPipelines(m_ctx.device(), nullptr, 1, &pipelineInfo, nullptr, &m_pipelineMask) != VK_SUCCESS) {
        showError("Failed to create the selection mask pipeline");
        return false;
    }
    return true;
}

bool SelectionOutlinePass::createCompositePipeline()
{
    const char *entryPoint = "main";
    const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages
    {
        VkPipelineShaderStageCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = m_outlineVertexShader,
            .pName = entryPoint
        },
        VkPipelineShaderStageCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = m_outlineFragmentShader,
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
    VkPipelineRasterizationStateCreateInfo rasterInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
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
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachState
    {
        .blendEnable = VK_TRUE,
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

    const std::array<VkDynamicState, 2> dynamicStates
    {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicStateInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };

    constexpr VkFormat colorFormat = Swapchain::ColorFormat;
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
        .layout = m_outlineLayout,
        .renderPass = VK_NULL_HANDLE
    };

    if (vkCreateGraphicsPipelines(m_ctx.device(), nullptr, 1, &pipelineInfo, nullptr, &m_pipelineOutline) != VK_SUCCESS) {
        showError("Failed to create the selection outline pipeline");
        return false;
    }
    return true;
}
