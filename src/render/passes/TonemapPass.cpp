#include "TonemapPass.h"
#include <volk.h>
#include "../core/Swapchain.h"
#include "../core/VulkanContext.h"
#include "../../assets/Mesh.h"
#include "../../common/errors.h"
#include "array"

bool TonemapPass::createResources() {
    VkSamplerCreateInfo sampler_info
    {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
    };
    if (vkCreateSampler(m_ctx.device(), &sampler_info, nullptr, &m_sampler) != VK_SUCCESS) {
        showError("Failed to create the tonemapping hdr render target sampler");
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
    if (vkCreateDescriptorSetLayout(m_ctx.device(), &layoutInfo, nullptr, &m_setLayout) != VK_SUCCESS) {
        showError("Failed to create the Tonemap descriptor set layout");
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
        .maxSets =1,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize
    };
    if (vkCreateDescriptorPool(m_ctx.device(), &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        showError("Failed to create the Tonemap descriptor pool");
        return false;
    }
    VkDescriptorSetAllocateInfo allocInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &m_setLayout
    };
    if (vkAllocateDescriptorSets(m_ctx.device(), &allocInfo, &m_set) != VK_SUCCESS) {
        showError("Failed to allocate hdr render target descriptor sets");
        return false;
    }
    VkPushConstantRange pushConstantRange
    {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(TonemapConstants)
    };
    VkPipelineLayoutCreateInfo pipelineLayoutInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &m_setLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange

    };
    if (vkCreatePipelineLayout(m_ctx.device(), &pipelineLayoutInfo, nullptr, &m_layout) != VK_SUCCESS) {
        showError("Failed to create the Tonemap pipeline layout");
        return false;
    }

    return true;
}

void TonemapPass::setSourceView(VkImageView hdrView) {
    VkDescriptorImageInfo imageInfo
    {
        .sampler = m_sampler,
        .imageView = hdrView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet writeDescriptorSet
    {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &imageInfo
    };
    vkUpdateDescriptorSets(m_ctx.device(), 1, &writeDescriptorSet, 0, nullptr);
}

bool TonemapPass::createPipelines() {
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
    VkPipelineRasterizationStateCreateInfo rasterInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .cullMode = VK_CULL_MODE_NONE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multiSampleInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo depthStencilInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE
    };
    VkPipelineColorBlendAttachmentState colorBlendAttachmentState{
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo colorBlendInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachmentState
    };

    const std::array<VkDynamicState, 2> dynamicStates
    {
        VK_DYNAMIC_STATE_VIEWPORT , VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicStateInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };

    constexpr VkFormat colAttachmentFormat = Swapchain::ColorFormat;
    constexpr VkFormat depthAttachmentFormat = Swapchain::DepthFormat;

    VkPipelineRenderingCreateInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colAttachmentFormat,
        .depthAttachmentFormat = depthAttachmentFormat
    };
    VkGraphicsPipelineCreateInfo pipelineInfo
    {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingInfo,
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
    if (vkCreateGraphicsPipelines(m_ctx.device(),nullptr,1,&pipelineInfo,nullptr,&m_pipeline)
                                                                                != VK_SUCCESS) {
        showError("Failed to create tonemap pass");
        return false;
    }
    return true;
}

void TonemapPass::appendShaderPrograms(std::vector<ShaderProgram> &out) {
    out.push_back({"tonemap.vert","tonemap.frag", &m_vertexShader, &m_fragmentShader,
        {&m_pipeline},[this]{return createPipelines();}});
}

void TonemapPass::transitionSource(VkCommandBuffer cmd,VkImage hdrImage) const {
    VkImageMemoryBarrier2 toSampled
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image = hdrImage,
        .subresourceRange {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .levelCount = 1, .layerCount = 1}
    };
    VkDependencyInfo depInfo
    {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toSampled,
    };
    vkCmdPipelineBarrier2(cmd,&depInfo);
}


void TonemapPass::record(VkCommandBuffer cmd, VkExtent2D extent) const{
    VkViewport viewport
    {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    vkCmdSetViewport(cmd,0,1,&viewport);

    VkRect2D scissor {.offset = {0,0},.extent = extent};
    vkCmdSetScissor(cmd,0,1,&scissor);


    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,m_pipeline);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,m_layout,
                            0 , 1 , &m_set, 0, nullptr);

    vkCmdPushConstants(cmd,m_layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(TonemapConstants),&m_settings);
    vkCmdDraw(cmd,3,1,0,0);
}

void TonemapPass::destroy() {
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
    for (VkShaderModule *m : {&m_vertexShader,&m_fragmentShader}) {
        if (*m) {
            vkDestroyShaderModule(device, *m, nullptr);
            *m = nullptr;
        }
    }
    if (m_pool) {
        vkDestroyDescriptorPool(device, m_pool, nullptr);
        m_pool = nullptr;
    }
    if (m_setLayout) {
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        m_setLayout = nullptr;
    }
    if (m_sampler) {
        vkDestroySampler(device, m_sampler, nullptr);
        m_sampler = nullptr;
    }
}

