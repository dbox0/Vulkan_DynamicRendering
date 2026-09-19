#include "SkyboxPass.h"
#include <volk.h>
#include <array>
#include <iostream>
#include "../core/Swapchain.h"
#include "../core/VulkanContext.h"
#include "../../common/errors.h"

bool SkyboxPass::createResources(VkDescriptorSetLayout globalLayout)
{
    const VkPushConstantRange pushConstantRange
    {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(SkyboxConstants)
    };
    const VkPipelineLayoutCreateInfo layoutInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &globalLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange
    };
    if (vkCreatePipelineLayout(m_ctx.device(), &layoutInfo, nullptr, &m_layout) != VK_SUCCESS) {
        showError("Failed to create the skybox pipeline layout");
        return false;
    }
    return true;
}

bool SkyboxPass::createPipelines()
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

    // The triangle is generated from gl_VertexIndex.
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
        .cullMode = VK_CULL_MODE_NONE,
        .lineWidth = 1.0f
    };
    VkPipelineMultisampleStateCreateInfo multiSampleInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };
    // Tests but never writes. The sky is emitted at 0.0 (reverese Z)
    VkPipelineDepthStencilStateCreateInfo depthStencilInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,   // reverse Z
        .stencilTestEnable = VK_FALSE
    };
    VkPipelineColorBlendAttachmentState colorBlendAttachmentState
    {
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    VkPipelineColorBlendStateCreateInfo colorBlendInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachmentState
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

    // The HDR scope
    constexpr VkFormat colorFormat = Swapchain::HDRFormat;
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
        .pMultisampleState = &multiSampleInfo,
        .pDepthStencilState = &depthStencilInfo,
        .pColorBlendState = &colorBlendInfo,
        .pDynamicState = &dynamicStateInfo,
        .layout = m_layout,
        .renderPass = VK_NULL_HANDLE
    };
    if (vkCreateGraphicsPipelines(m_ctx.device(), nullptr, 1, &pipelineInfo,
                                  nullptr, &m_pipeline) != VK_SUCCESS) {
        showError("Failed to create the skybox pipeline");
        return false;
    }
    return true;
}

void SkyboxPass::appendShaderPrograms(std::vector<ShaderProgram> &out)
{
    out.push_back({ "skybox/skybox.vert", "skybox/skybox.frag",
                    &m_vertexShader, &m_fragmentShader,
                    { &m_pipeline }, [this] { return createPipelines(); } });
}

void SkyboxPass::record(VkCommandBuffer cmd, VkDescriptorSet globalSet,
                        const glm::mat4 &invViewProj, const glm::vec3 &cameraPosition,
                        uint32_t cubeSlot) const
{
    if (!m_settings.enabled || cubeSlot == 0) {
        return;
    }
    //if this fires, createPipelines() was never called or failed,
    if (!m_pipeline) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            std::cerr << "[skybox] no pipeline; createPipelines() was not called"
                      << std::endl;
        }
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout,
                            0, 1, &globalSet, 0, nullptr);

    const SkyboxConstants consts
    {
        .invViewProj    = invViewProj,
        .cameraPosition = cameraPosition,
        .cubeSlot       = cubeSlot,
        .intensity      = m_settings.intensity,
        .lod            = m_settings.lod
    };
    vkCmdPushConstants(cmd, m_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(consts), &consts);

    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void SkyboxPass::destroy()
{
    const VkDevice device = m_ctx.device();
    if (!device) {
        return;
    }
    if (m_pipeline) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = nullptr;
    }
    if (m_layout) {
        vkDestroyPipelineLayout(device, m_layout, nullptr);
        m_layout = nullptr;
    }
    for (VkShaderModule *m : { &m_vertexShader, &m_fragmentShader }) {
        if (*m) {
            vkDestroyShaderModule(device, *m, nullptr);
            *m = nullptr;
        }
    }
}
