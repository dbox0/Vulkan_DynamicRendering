#include "DebugLinePass.h"

#include <volk.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "../core/Swapchain.h"
#include "../core/VulkanContext.h"
#include "../../common/errors.h"
#include "../../scene/Scene.h"

void DebugLinePass::appendShaderPrograms(std::vector<ShaderProgram> &out, VkPipelineLayout layout)
{
    out.push_back({ "debug_line.vert", "debug_line.frag", &m_vertexShader, &m_fragmentShader,
                    { &m_pipeline }, [this, layout] { return createPipelines(layout); } });
}

void DebugLinePass::destroy()
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
}


void DebugLinePass::beginFrame()
{
    m_vertices.clear();
    m_vertexCount = 0;
}

void DebugLinePass::addLine(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &color)
{
    m_vertices.push_back(DebugVertex{ a, color });
    m_vertices.push_back(DebugVertex{ b, color });
}

void DebugLinePass::collectLightGizmos(Scene &scene)
{
    const NodeWorld &nodes = scene.nodes();
    const size_t count = nodes.size();

    for (uint32_t nodeId = 1; nodeId <= count; ++nodeId) {
        if (!nodes.isAlive(nodeId)) {
            continue;
        }
        const Node &node = nodes.getNode(nodeId);
        if (node.lightType != LightType::Directional) {
            continue;
        }

        const glm::mat4 &world = nodes.worldMatrix(nodeId);
        const glm::vec3 origin = glm::vec3(world[3]);

        // -Z of the world matrix, the same direction the shader is lit with.
        // Normalised otherwsie the gizmo would be streched.
        glm::vec3 forward = -glm::vec3(world[2]);
        const float len = glm::length(forward);
        forward = len > 1e-6f ? forward / len : glm::vec3(0.0f, -1.0f, 0.0f);

        const glm::vec3 color = node.lightColor;

        constexpr float kHandle = 0.25f;   // the clickable blob, matches pickLight
        constexpr float kRay    = 2.0f;

        // Handle: three axis-aligned segments
        addLine(origin - glm::vec3(kHandle, 0, 0), origin + glm::vec3(kHandle, 0, 0), color);
        addLine(origin - glm::vec3(0, kHandle, 0), origin + glm::vec3(0, kHandle, 0), color);
        addLine(origin - glm::vec3(0, 0, kHandle), origin + glm::vec3(0, 0, kHandle), color);

        const glm::vec3 tip = origin + forward * kRay;
        addLine(origin, tip, color);

        // Arrowhead
        const glm::vec3 up = std::abs(forward.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 right = glm::normalize(glm::cross(forward, up));
        const glm::vec3 realUp = glm::cross(right, forward);

        const glm::vec3 back = tip - forward * 0.35f;
        addLine(tip, back + right  * 0.15f, color);
        addLine(tip, back - right  * 0.15f, color);
        addLine(tip, back + realUp * 0.15f, color);
        addLine(tip, back - realUp * 0.15f, color);
    }
}

void DebugLinePass::upload(DebugVertex *dst)
{
    const auto count = static_cast<uint32_t>(
        std::min<size_t>(m_vertices.size(), MaxVertices));

    if (count != 0) {
        std::memcpy(dst, m_vertices.data(), count * sizeof(DebugVertex));
    }
    m_vertexCount = count;
}

void DebugLinePass::record(VkCommandBuffer cmd, VkPipelineLayout layout,
                           const FrameConstants &frameConsts, uint64_t lineBufferAddress) const
{
    if (m_vertexCount == 0) {
        return;
    }

    // The address slot that holds the mesh vertex buffer is repointed at the
    // line buffer -- debug_line.vert reads the same push constant block, so
    // this needs no layout of its own.
    FrameConstants debugConsts = frameConsts;
    debugConsts.vertexBufferAddress = lineBufferAddress;

    vkCmdPushConstants(cmd, layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(FrameConstants), &debugConsts);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdDraw(cmd, m_vertexCount, 1, 0, 0);
}

// ============================================================================
// pipeline
// ============================================================================

bool DebugLinePass::createPipelines(VkPipelineLayout layout)
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
        .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST
    };
    VkPipelineViewportStateCreateInfo viewportInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1
    };

    // lineWidth stays 1.0: wideLines is a device feature and not worth
    // requiring for an overlay.
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

    // Tests but does not write: the gizmo is occluded by geometry in front of
    // it, which is the depth cue that tells where the light actually is,
    // but it must not push anything out of the depth buffer behind it!!
    VkPipelineDepthStencilStateCreateInfo depthStencilInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_GREATER
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachState
    {
        .blendEnable = VK_FALSE,
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
        .depthAttachmentFormat = Swapchain::DepthFormat
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
        showError("Failed to create the debug line pipeline");
        return false;
    }
    return true;
}
