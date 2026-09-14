#include "Renderer.h"

#include <volk.h>
#include <vk_mem_alloc.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "GeometryStore.h"
#include "ResourceStore.h"
#include "Swapchain.h"
#include "VulkanContext.h"
#include "../common/errors.h"
#include "../common/constants.h"
#include "../scene/Camera.h"

// ============================================================================
// lifetime
// ============================================================================

bool Renderer::initialize(uint32_t maxDrawsPerFrame)
{
    m_maxDraws = maxDrawsPerFrame;
    m_drawItems.reserve(maxDrawsPerFrame);

    if (!createShaders()) {
        showError("Error creating shader modules");
        return false;
    }
    if (!createPipeline(false, m_pipelineOpaque) || !createPipeline(true, m_pipelineBlend)) {
        showError("Unable to initialize the graphics pipeline");
        return false;
    }
    // Order matters: createMaskPipeline reuses m_pipelineLayout, which the
    // call above is what creates.
    if (!createOutlineDescriptors() || !createMaskPipeline() || !createOutlinePipeline()) {
        showError("Unable to initialize the selection outline pipelines");
        return false;
    }
    if (!createSyncResources()) {
        showError("Could not create the sync resources");
        return false;
    }
    if (!createCommandBuffers()) {
        showError("Could not create the command buffer objects");
        return false;
    }
    if (!createFrameBuffers(maxDrawsPerFrame)) {
        showError("Could not create the indirect draw buffers");
        return false;
    }

    // Optional: no file, or a broken one, just leaves m_envSlot at 0 and the
    // shader falls back to the hemisphere ambient. Not worth failing init over.
    if (const uint32_t envTextureId = m_resources.loadEnvironment(ASSET_DIR "env/env_test.hdr")) {
        const uint32_t envImageId = m_resources.texture(envTextureId).imageId;
        m_envSlot   = envTextureId - 1;
        m_envMaxLod = static_cast<float>(m_resources.imageMipLevels(envImageId) - 1);
        std::cout << "Environment map loaded into descriptor slot " << m_envSlot << std::endl;
    }

    // The swapchain (and therefore the mask image) already exists by the time
    // the renderer is initialised, so the descriptor can be pointed at it now.
    updateSelectionMaskDescriptor();
    return true;
}

void Renderer::shutdown()
{
    if (!m_ctx.device()) {
        return;
    }

    // Exactly one destroy per handle. The old shutdown() ran this loop twice.
    for (FrameResources &res : m_frameResources) {
        if (res.imageAcquiredSemaphore) {
            vkDestroySemaphore(m_ctx.device(), res.imageAcquiredSemaphore, nullptr);
            res.imageAcquiredSemaphore = nullptr;
        }
        if (res.commandPool) {
            // Frees res.commandBuffer implicitly.
            vkDestroyCommandPool(m_ctx.device(), res.commandPool, nullptr);
            res.commandPool = nullptr;
            res.commandBuffer = nullptr;
        }
        if (res.indirectDrawBuffer.allocation) {
            vmaUnmapMemory(m_ctx.allocator(), res.indirectDrawBuffer.allocation);
            res.indirectDrawPtr = nullptr;
        }
        m_ctx.destroyBuffer(res.indirectDrawBuffer);

        if (res.renderItemBuffer.allocation) {
            vmaUnmapMemory(m_ctx.allocator(), res.renderItemBuffer.allocation);
            res.renderItemPtr = nullptr;
        }
        m_ctx.destroyBuffer(res.renderItemBuffer);

        if (res.frameDataPtr) {
            vmaUnmapMemory(m_ctx.allocator(), res.frameDataBuffer.allocation);
            res.frameDataPtr = nullptr;
        }
        m_ctx.destroyBuffer(res.frameDataBuffer);
    }

    if (m_timelineSemaphore) {
        vkDestroySemaphore(m_ctx.device(), m_timelineSemaphore, nullptr);
        m_timelineSemaphore = nullptr;
    }

    for (VkPipeline *p : { &m_pipelineOpaque, &m_pipelineBlend,
                           &m_pipelineMask, &m_pipelineOutline }) {
        if (*p) {
            vkDestroyPipeline(m_ctx.device(), *p, nullptr);
            *p = nullptr;
        }
    }

    for (VkPipelineLayout *l : { &m_pipelineLayout, &m_outlineLayout }) {
        if (*l) {
            vkDestroyPipelineLayout(m_ctx.device(), *l, nullptr);
            *l = nullptr;
        }
    }

    for (VkShaderModule *m : { &m_vertexShader, &m_fragmentShader,
                               &m_maskVertexShader, &m_maskFragmentShader,
                               &m_outlineVertexShader, &m_outlineFragmentShader }) {
        if (*m) {
            vkDestroyShaderModule(m_ctx.device(), *m, nullptr);
            *m = nullptr;
        }
    }

    // Frees m_outlineSet with it.
    if (m_outlinePool) {
        vkDestroyDescriptorPool(m_ctx.device(), m_outlinePool, nullptr);
        m_outlinePool = nullptr;
        m_outlineSet  = nullptr;
    }
    if (m_outlineSetLayout) {
        vkDestroyDescriptorSetLayout(m_ctx.device(), m_outlineSetLayout, nullptr);
        m_outlineSetLayout = nullptr;
    }
    if (m_maskSampler) {
        vkDestroySampler(m_ctx.device(), m_maskSampler, nullptr);
        m_maskSampler = nullptr;
    }
}

// ============================================================================
// shaders
// ============================================================================

namespace {
    std::string readTextFile(const std::string &filePath)
    {
        std::ifstream infile(filePath);
        if (!infile.is_open()) {
            return {};
        }
        std::stringstream buff;
        buff << infile.rdbuf();
        return buff.str();
    }
}

VkShaderModule Renderer::createShaderModule(const std::string &fileName, shaderc_shader_kind kind) const
{
    const std::string shaderPath = SHADER_DIR + fileName;
    const std::string src = readTextFile(shaderPath);
    if (src.empty()) {
        showError("Shader file does not exist or is empty: " + shaderPath);
        return nullptr;
    }

    std::cout << "Compiling shader: " << shaderPath << std::endl;

    shaderc::Compiler compiler;
    shaderc::CompileOptions opts;
    opts.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_4);
    opts.SetTargetSpirv(shaderc_spirv_version_1_6);
    opts.SetOptimizationLevel(shaderc_optimization_level_performance);

    const shaderc::SpvCompilationResult result =
        compiler.CompileGlslToSpv(src, kind, fileName.c_str(), opts);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        std::cerr << "Shader compilation error: " << result.GetErrorMessage() << std::endl;
        return nullptr;
    }

    const size_t shaderSize = (result.cend() - result.cbegin()) * sizeof(uint32_t);

    VkShaderModuleCreateInfo shaderModuleCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shaderSize,
        .pCode = result.cbegin()
    };

    VkShaderModule shaderModule = nullptr;
    if (vkCreateShaderModule(m_ctx.device(), &shaderModuleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        showError("Failed to create shader module");
        return nullptr;
    }
    return shaderModule;
}

bool Renderer::createShaders()
{
    m_vertexShader = createShaderModule("pbr.vert", shaderc_vertex_shader);
    if (!m_vertexShader) {
        return false;
    }
    m_fragmentShader = createShaderModule("pbr.frag", shaderc_fragment_shader);
    if (!m_fragmentShader) {
        return false;
    }

    m_maskVertexShader   = createShaderModule("selection_mask.vert", shaderc_vertex_shader);
    m_maskFragmentShader = createShaderModule("selection_mask.frag", shaderc_fragment_shader);
    m_outlineVertexShader   = createShaderModule("outline.vert", shaderc_vertex_shader);
    m_outlineFragmentShader = createShaderModule("outline.frag", shaderc_fragment_shader);

    return m_maskVertexShader && m_maskFragmentShader &&
           m_outlineVertexShader && m_outlineFragmentShader;
}

// ============================================================================
// pipeline
// ============================================================================

bool Renderer::createPipeline(bool blendEnabled, VkPipeline &outPipeline)
{
    if (!m_pipelineLayout) {
        VkPushConstantRange pushConstantRange
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size = sizeof(FrameConstants)
        };

        const std::array<VkDescriptorSetLayout, 1> dsLayouts{ m_resources.globalLayout() };

        VkPipelineLayoutCreateInfo pipelineLayoutInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = static_cast<uint32_t>(dsLayouts.size()),
            .pSetLayouts = dsLayouts.data(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pushConstantRange
        };

        if (vkCreatePipelineLayout(m_ctx.device(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            showError("Failed to create the pipeline layout");
            return false;
        }
    }

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
        .depthCompareOp = VK_COMPARE_OP_LESS,
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
        .layout = m_pipelineLayout,
        .renderPass = VK_NULL_HANDLE,
    };

    if (vkCreateGraphicsPipelines(m_ctx.device(), nullptr, 1, &pipelineInfo, nullptr, &outPipeline) != VK_SUCCESS) {
        showError("Failed to create the graphics pipeline");
        return false;
    }
    return true;
}

// ============================================================================
// selection outline
// ============================================================================

bool Renderer::createOutlineDescriptors()
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
    return true;
}

void Renderer::updateSelectionMaskDescriptor()
{
    if (!m_outlineSet || !m_swapchain.selectionMaskImageView()) {
        return;
    }

    // Safe to rewrite in place: this is only ever called from initialize() or
    // straight after Swapchain::recreate(), which waits idle first.
    VkDescriptorImageInfo imageInfo
    {
        .sampler = m_maskSampler,
        .imageView = m_swapchain.selectionMaskImageView(),
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

bool Renderer::createMaskPipeline()
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

    // No culling: a mask is coverage, and back faces of an open or
    // double-sided mesh are just as much part of the silhouette.
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

    // No depth attachment at all, hence no depth state. The selection is
    // marked whether or not it is occluded, which is what makes the outline
    // visible through walls -- useful in an editor, and it avoids the mess of
    // a half-occluded object outlining its own visible fragment boundaries.
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

    constexpr VkFormat maskFormat = Swapchain::SelectionMaskFormat;
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
        .layout = m_pipelineLayout,
        .renderPass = VK_NULL_HANDLE
    };

    if (vkCreateGraphicsPipelines(m_ctx.device(), nullptr, 1, &pipelineInfo, nullptr, &m_pipelineMask) != VK_SUCCESS) {
        showError("Failed to create the selection mask pipeline");
        return false;
    }
    return true;
}

bool Renderer::createOutlinePipeline()
{
    VkPushConstantRange pushConstantRange
    {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(OutlineConstants)
    };
    VkPipelineLayoutCreateInfo layoutInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &m_outlineSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange
    };
    if (vkCreatePipelineLayout(m_ctx.device(), &layoutInfo, nullptr, &m_outlineLayout) != VK_SUCCESS) {
        showError("Failed to create the outline pipeline layout");
        return false;
    }

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

    // Depth is off but the format still has to be declared: a dynamic
    // rendering pipeline must describe the same attachments as the
    // VkRenderingInfo it is used inside, and this draws in the scene pass.
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
        .layout = m_outlineLayout,
        .renderPass = VK_NULL_HANDLE
    };

    if (vkCreateGraphicsPipelines(m_ctx.device(), nullptr, 1, &pipelineInfo, nullptr, &m_pipelineOutline) != VK_SUCCESS) {
        showError("Failed to create the selection outline pipeline");
        return false;
    }
    return true;
}

void Renderer::accumulateSelectionBounds(const glm::mat4 &viewProj, const glm::mat4 &worldMatrix,
                                         const SubMesh &subMesh)
{
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

VkRect2D Renderer::selectionScissor() const
{
    const auto width  = static_cast<int32_t>(m_swapchain.width());
    const auto height = static_cast<int32_t>(m_swapchain.height());

    const VkRect2D fullScreen
    {
        .offset{ 0, 0 },
        .extent{ m_swapchain.width(), m_swapchain.height() }
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
    const auto pad = static_cast<float>(m_outline.thickness) + 2.0f;

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

void Renderer::recordSelectionMask(FrameResources &res)
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
        .image = m_swapchain.selectionMaskImage(),
        .subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
    };
    VkDependencyInfo toAttachmentDep
    {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toAttachment
    };
    vkCmdPipelineBarrier2(res.commandBuffer, &toAttachmentDep);

    VkRenderingAttachmentInfo maskAttachInfo
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = m_swapchain.selectionMaskImageView(),
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
        .renderArea
        {
            .offset{ 0, 0 },
            .extent{ m_swapchain.width(), m_swapchain.height() }
        },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &maskAttachInfo
    };

    vkCmdBeginRendering(res.commandBuffer, &maskRenderingInfo);
    {
        // Identical to the scene viewport, flip included, or the mask lands
        // upside down relative to the image it is meant to outline.
        VkViewport viewport
        {
            .x = 0,
            .y = static_cast<float>(m_swapchain.height()),
            .width  =  static_cast<float>(m_swapchain.width()),
            .height = -static_cast<float>(m_swapchain.height()),
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };
        vkCmdSetViewport(res.commandBuffer, 0, 1, &viewport);

        VkRect2D scissor
        {
            .offset{ 0, 0 },
            .extent{ m_swapchain.width(), m_swapchain.height() }
        };
        vkCmdSetScissor(res.commandBuffer, 0, 1, &scissor);

        vkCmdBindPipeline(res.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineMask);

        // One indirect draw per selected slot. The sort in writeDrawCommands
        // scatters a node's submeshes across buckets, so they are not
        // contiguous -- but a selection is a handful of draws, not thousands.
        for (const uint32_t slot : m_selectedSlots) {
            vkCmdDrawIndexedIndirect(
                res.commandBuffer, res.indirectDrawBuffer.vkBuffer,
                static_cast<VkDeviceSize>(slot) * sizeof(VkDrawIndexedIndirectCommand),
                1, sizeof(VkDrawIndexedIndirectCommand));
        }
    }
    vkCmdEndRendering(res.commandBuffer);

    VkImageMemoryBarrier2 toSampled
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image = m_swapchain.selectionMaskImage(),
        .subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
    };
    VkDependencyInfo toSampledDep
    {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toSampled
    };
    vkCmdPipelineBarrier2(res.commandBuffer, &toSampledDep);
}

// ============================================================================
// per-frame resources
// ============================================================================

bool Renderer::createSyncResources()
{
    // A timeline semaphore is a monotonically increasing 64-bit counter the
    // CPU can wait on, which is what paces frames in flight.
    VkSemaphoreTypeCreateInfo semaphoreTypeInfo
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = MaxFramesInFlight
    };
    VkSemaphoreCreateInfo timelineInfo
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &semaphoreTypeInfo
    };

    if (vkCreateSemaphore(m_ctx.device(), &timelineInfo, nullptr, &m_timelineSemaphore) != VK_SUCCESS) {
        showError("Failed to create the timeline semaphore");
        return false;
    }

    for (FrameResources &res : m_frameResources) {
        VkSemaphoreCreateInfo semaphoreInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        if (vkCreateSemaphore(m_ctx.device(), &semaphoreInfo, nullptr, &res.imageAcquiredSemaphore) != VK_SUCCESS) {
            showError("Failed to create the per-frame image-acquired semaphore");
            return false;
        }
    }
    return true;
}

bool Renderer::createCommandBuffers()
{
    for (FrameResources &res : m_frameResources) {
        VkCommandPoolCreateInfo poolInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .queueFamilyIndex = m_ctx.gfxFamily()
        };

        if (vkCreateCommandPool(m_ctx.device(), &poolInfo, nullptr, &res.commandPool) != VK_SUCCESS) {
            showError("Unable to create the command buffer pool");
            return false;
        }

        VkCommandBufferAllocateInfo cmdAllocInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = res.commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        if (vkAllocateCommandBuffers(m_ctx.device(), &cmdAllocInfo, &res.commandBuffer) != VK_SUCCESS) {
            showError("Unable to allocate the command buffer");
            return false;
        }
    }
    return true;
}

bool Renderer::createFrameBuffers(uint32_t maxDrawsPerFrame)
{
    // Sized by DRAW count, not node count. A node with N primitives emits N
    // draws, so sizing these by maxNodes() overflows on any multi-primitive
    // mesh -- which the Mario Kart scene is full of.
    const size_t indirectBytes   = static_cast<size_t>(maxDrawsPerFrame) * sizeof(VkDrawIndexedIndirectCommand);
    const size_t renderItemBytes = static_cast<size_t>(maxDrawsPerFrame) * sizeof(RenderItem);

    // Every per-frame buffer here is host-visible and persistently mapped:
    // written once per frame by the CPU, read straight from host memory by
    // the GPU. On ReBAR hardware that beats staging + copy for data this small.
    auto createMapped = [this](VkBufferUsageFlags usage, size_t bytes,
                               GPUBuffer &outBuffer, void *&outPtr, const char *what) -> bool
    {
        outBuffer = m_ctx.createBuffer(usage, bytes, true, VMA_MEMORY_USAGE_AUTO);
        if (!outBuffer.vkBuffer) {
            showError(std::string("Unable to create the ") + what);
            return false;
        }
        if (vmaMapMemory(m_ctx.allocator(), outBuffer.allocation, &outPtr) != VK_SUCCESS) {
            showError(std::string("Unable to map the ") + what);
            return false;
        }
        return true;
    };

    for (FrameResources &res : m_frameResources) {
        void *ptr = nullptr;

        // Indirect commands. Not read through BDA, so no device address needed.
        if (!createMapped(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, indirectBytes,
                          res.indirectDrawBuffer, ptr, "indirect draw buffer")) {
            return false;
        }
        res.indirectDrawPtr = static_cast<VkDrawIndexedIndirectCommand *>(ptr);

        // Per-draw world/normal matrices + material index, indexed by
        // gl_InstanceIndex in the vertex shader.
        if (!createMapped(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                          renderItemBytes, res.renderItemBuffer, ptr, "render item buffer")) {
            return false;
        }
        res.renderItemPtr = static_cast<RenderItem *>(ptr);

        // Camera + lighting for this frame. One struct, not an array -- but
        // still per frame-in-flight, because the other frame may still be
        // reading its copy on the GPU.
        if (!createMapped(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                          sizeof(FrameData), res.frameDataBuffer, ptr, "frame data buffer")) {
            return false;
        }
        res.frameDataPtr = static_cast<FrameData *>(ptr);
    }
    return true;
}

// ============================================================================
// draw recording
// ============================================================================

uint32_t Renderer::writeDrawCommands(FrameResources &res, const glm::mat4 &viewProj)
{
    for (DrawBatch &batch : m_batches) {
        batch = DrawBatch{};
    }

    const uint32_t drawCount = static_cast<uint32_t>(std::min<size_t>(m_drawItems.size(), m_maxDraws));

    if (m_drawItems.size() > m_maxDraws) {
        std::cerr << "[warn] Draw list of " << m_drawItems.size()
                  << " exceeds the per-frame limit of " << m_maxDraws
                  << "; clamping" << std::endl;
    }

    // Bucket 0/1 = opaque single/double sided, 2/3 = blended single/double.
    // Each bucket is one contiguous indirect draw with its own cull mode and
    // pipeline, which is why the sort has to happen before anything is written.
    m_selectedSlots.clear();
    m_selectionBoundsValid = true;
    m_selectionMin = glm::vec2( std::numeric_limits<float>::max());
    m_selectionMax = glm::vec2(-std::numeric_limits<float>::max());

    m_sorted.clear();
    m_sorted.reserve(drawCount);

    for (uint32_t i = 0; i < drawCount; ++i) {
        const SubMesh &subMesh = *m_drawItems[i].subMesh;
        const uint32_t materialId = subMesh.materialId ? subMesh.materialId
                                                       : m_resources.defaultMaterialId();
        const Material &material = m_resources.material(materialId);

        const uint32_t bucket = (material.alphaMode == AlphaMode::Blend ? 2u : 0u)
                              + (material.doubleSided ? 1u : 0u);

        // w of the clip-space origin is the view depth. Crude -- per-object,
        // not per-triangle -- but it is what makes blended geometry stack in
        // the right order without a real sorted transparency pass.
        const glm::vec4 clip = viewProj * glm::vec4(glm::vec3(m_drawItems[i].worldMatrix[3]), 1.0f);
        m_sorted.push_back(SortedDraw{ bucket, clip.w, i });
    }

    std::stable_sort(m_sorted.begin(), m_sorted.end(),
        [](const SortedDraw &a, const SortedDraw &b)
        {
            if (a.bucket != b.bucket) return a.bucket < b.bucket;
            if (a.bucket < 2)         return false;        // opaque: submission order
            return a.depth > b.depth;                      // blended: far to near
        });

    for (uint32_t slot = 0; slot < drawCount; ++slot) {
        const SortedDraw &sorted = m_sorted[slot];
        const DrawItem &item = m_drawItems[sorted.index];
        const SubMesh &subMesh = *item.subMesh;

        res.indirectDrawPtr[slot] = VkDrawIndexedIndirectCommand
        {
            .indexCount = static_cast<uint32_t>(subMesh.indexCount),
            .instanceCount = 1,
            .firstIndex = static_cast<uint32_t>(subMesh.indexStart),
            .vertexOffset = static_cast<int32_t>(subMesh.vertexStart),
            .firstInstance = slot
        };

        const uint32_t materialIndex = subMesh.materialId ? subMesh.materialId - 1 : 0;

        res.renderItemPtr[slot] = RenderItem
        {
            .worldMatrix   = item.worldMatrix,
            .materialIndex = materialIndex
        };

        // Recorded after the sort, because the mask pass replays these exact
        // slots out of the indirect buffer.
        if (m_selectedNode != 0 && item.nodeId == m_selectedNode) {
            m_selectedSlots.push_back(slot);
            accumulateSelectionBounds(viewProj, item.worldMatrix, subMesh);
        }

        DrawBatch &batch = m_batches[sorted.bucket];
        if (batch.count == 0) {
            batch.first = slot;
        }
        ++batch.count;
    }

    for (uint32_t b = 0; b < m_batches.size(); ++b) {
        m_batches[b].cullMode = (b & 1u) ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
        m_batches[b].blend    = b >= 2;
    }
    return drawCount;
}

void Renderer::recordCommandBuffer(FrameResources &res, uint32_t imageIndex, uint32_t drawCount,
                                   const std::function<void(VkCommandBuffer)> &overlay)
{
    VkCommandBufferBeginInfo cmdBeginInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    vkBeginCommandBuffer(res.commandBuffer, &cmdBeginInfo);

    // UNDEFINED -> attachment layouts for colour and depth.
    const std::array<VkImageMemoryBarrier2, 2> layoutBarriers
    {
        VkImageMemoryBarrier2
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image = m_swapchain.image(imageIndex),
            .subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
        },
        VkImageMemoryBarrier2
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .image = m_swapchain.depthImage(),
            .subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 }
        }
    };
    VkDependencyInfo depInfo
    {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = static_cast<uint32_t>(layoutBarriers.size()),
        .pImageMemoryBarriers = layoutBarriers.data()
    };
    vkCmdPipelineBarrier2(res.commandBuffer, &depInfo);

    VkRenderingAttachmentInfo colorAttachInfo
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = m_swapchain.imageView(imageIndex),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue{ .color{ 0.3f, 0.3f, 1.0f, 1.0f } }
    };
    VkRenderingAttachmentInfo depthAttachInfo
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = m_swapchain.depthImageView(),
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue{ .depthStencil{ 1.0f, 0 } }
    };
    VkRenderingInfo renderingInfo
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea
        {
            .offset{ .x = 0, .y = 0 },
            .extent{ .width = m_swapchain.width(), .height = m_swapchain.height() }
        },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachInfo,
        .pDepthAttachment = &depthAttachInfo
    };

    // Frame-wide state: bindless descriptor set + buffer addresses.
    VkDescriptorSet globalSet = m_resources.globalDescriptorSet();
    vkCmdBindDescriptorSets(res.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout, 0, 1, &globalSet, 0, nullptr);

    FrameConstants frameConsts
    {
        .vertexBufferAddress     = m_geometry.vertexBufferAddress(),
        .materialBufferAddress   = m_resources.materialBufferAddress(),
        .renderItemBufferAddress = res.renderItemBuffer.deviceAddress,
        .frameDataAddress        = res.frameDataBuffer.deviceAddress
    };
    vkCmdPushConstants(res.commandBuffer, m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(FrameConstants), &frameConsts);

    vkCmdBindIndexBuffer(res.commandBuffer, m_geometry.indexBuffer(), 0, VK_INDEX_TYPE_UINT32);

    // Its own rendering scope, before the scene pass, so the composite inside
    // that pass can sample a finished mask. Skipped entirely when nothing is
    // selected: no barriers, no clear, no cost.
    const bool hasSelection = !m_selectedSlots.empty();
    if (hasSelection) {
        recordSelectionMask(res);
    }

    vkCmdBeginRendering(res.commandBuffer, &renderingInfo);
    {
        // Negative height flips Y so the glTF/glm convention lands right side up.
        VkViewport viewport
        {
            .x = 0,
            .y = static_cast<float>(m_swapchain.height()),
            .width  =  static_cast<float>(m_swapchain.width()),
            .height = -static_cast<float>(m_swapchain.height()),
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };
        vkCmdSetViewport(res.commandBuffer, 0, 1, &viewport);

        VkRect2D scissor
        {
            .offset{ .x = 0, .y = 0 },
            .extent{ .width = m_swapchain.width(), .height = m_swapchain.height() }
        };
        vkCmdSetScissor(res.commandBuffer, 0, 1, &scissor);

        VkPipeline boundPipeline = nullptr;

        for (const DrawBatch &batch : m_batches) {
            if (batch.count == 0) {
                continue;
            }
            VkPipeline wantedPipeline = batch.blend? m_pipelineBlend : m_pipelineOpaque;

            if (wantedPipeline != boundPipeline) {
                vkCmdBindPipeline(res.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wantedPipeline);
                boundPipeline = wantedPipeline;
            }
            vkCmdSetCullMode(res.commandBuffer, batch.cullMode);
            vkCmdDrawIndexedIndirect(
                res.commandBuffer, res.indirectDrawBuffer.vkBuffer,
                static_cast<VkDeviceSize>(batch.first) * sizeof(VkDrawIndexedIndirectCommand),
                batch.count, sizeof(VkDrawIndexedIndirectCommand));
        }

        // Over the finished scene, under the UI.
        if (hasSelection) {
            const VkRect2D outlineScissor = selectionScissor();
            if (outlineScissor.extent.width != 0 && outlineScissor.extent.height != 0) {
                vkCmdBindPipeline(res.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineOutline);
                vkCmdBindDescriptorSets(res.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_outlineLayout, 0, 1, &m_outlineSet, 0, nullptr);
                vkCmdPushConstants(res.commandBuffer, m_outlineLayout,
                                   VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(OutlineConstants), &m_outline);

                // A fullscreen triangle clipped to the selection's screen box.
                // Without the scissor this would run the dilate over every
                // pixel on screen to light up a few hundred of them.
                vkCmdSetScissor(res.commandBuffer, 0, 1, &outlineScissor);
                vkCmdDraw(res.commandBuffer, 3, 1, 0, 0);
            }
        }

        // ImGui sets its own viewport and scissor per draw command, so the
        // narrowed scissor above does not leak into the overlay.
        if (overlay) {
            overlay(res.commandBuffer);
        }
    }
    vkCmdEndRendering(res.commandBuffer);

    // COLOR_ATTACHMENT -> PRESENT_SRC.
    VkImageMemoryBarrier2 presentLayoutBarrier
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image = m_swapchain.image(imageIndex),
        .subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
    };
    VkDependencyInfo presentDepInfo
    {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &presentLayoutBarrier
    };
    vkCmdPipelineBarrier2(res.commandBuffer, &presentDepInfo);

    vkEndCommandBuffer(res.commandBuffer);
}



// ============================================================================
// frame
// ============================================================================

void Renderer::render(Scene &scene, const Camera &camera, uint32_t windowWidth, uint32_t windowHeight,const std::function<void(VkCommandBuffer)> &overlay)
{
    if (m_swapchain.needsRecreate()) {
        if (!m_swapchain.recreate(windowWidth, windowHeight)) {
            return;
        }
        // New image, new view: the outline's descriptor still points at the
        // destroyed one. recreate() waits idle, so rewriting here is safe.
        updateSelectionMaskDescriptor();
    }

    const uint32_t frameResIndex = m_frameIndex++ % MaxFramesInFlight;
    const uint64_t signalValue   = m_nextSignalValue++;
    const uint64_t waitValue     = signalValue - MaxFramesInFlight;

    // Wait until this frame slot's previous submission has completed.
    VkSemaphoreWaitInfo waitInfo
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &m_timelineSemaphore,
        .pValues = &waitValue
    };
    vkWaitSemaphores(m_ctx.device(), &waitInfo, UINT64_MAX);

    FrameResources &res = m_frameResources[frameResIndex];
    vkResetCommandPool(m_ctx.device(), res.commandPool, 0);

    uint32_t imageIndex = 0;
    if (!m_swapchain.acquireNextImage(res.imageAcquiredSemaphore, imageIndex)) {
        // The frame slot never gets submitted, so the timeline would stall on
        // waitValue forever. Signal it manually to keep the counter in step.
        VkSemaphoreSignalInfo signalInfo
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
            .semaphore = m_timelineSemaphore,
            .value = signalValue
        };
        vkSignalSemaphore(m_ctx.device(), &signalInfo);
        return;
    }

    const float aspectRatio = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
    const glm::mat4 viewProj = camera.viewProjection(aspectRatio);

    *res.frameDataPtr = FrameData
    {
        .viewProj       = viewProj,
        .cameraPosition = camera.position,
        .exposure       = 1.0f,
        .sunDirection   = glm::normalize(glm::vec3(0.3f, -1.0f, -0.5f)),
        .sunIntensity   = 3.0f,
        .sunColor       = glm::vec3(1.0f, 0.96f, 0.9f),
        .envTex       = m_resources.environmentTextureId()
                            ? m_resources.environmentTextureId() - 1 : 0,
        .envIntensity = .5f,
        .envMaxLod    = m_envMaxLod,
    };

    scene.collectDrawItems(m_geometry, m_drawItems);
    const uint32_t drawCount = writeDrawCommands(res, viewProj);

    recordCommandBuffer(res, imageIndex, drawCount,overlay);

    VkSemaphoreSubmitInfo imageAcquireWaitInfo
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = res.imageAcquiredSemaphore,
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
    };

    VkSemaphore renderCompleteSemaphore = m_swapchain.renderCompleteSemaphore(imageIndex);
    const std::array<VkSemaphoreSubmitInfo, 2> semaphoreSignals
    {
        VkSemaphoreSubmitInfo
        {   // render work is done -> presentation may proceed
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = renderCompleteSemaphore,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT
        },
        VkSemaphoreSubmitInfo
        {   // whole frame is done -> the CPU may reuse this frame slot
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = m_timelineSemaphore,
            .value = signalValue,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
        }
    };

    VkCommandBufferSubmitInfo cmdSubmitInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = res.commandBuffer,
    };
    VkSubmitInfo2 submitInfo
    {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &imageAcquireWaitInfo,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmdSubmitInfo,
        .signalSemaphoreInfoCount = static_cast<uint32_t>(semaphoreSignals.size()),
        .pSignalSemaphoreInfos = semaphoreSignals.data()
    };
    vkQueueSubmit2(m_ctx.gfxQueue(), 1, &submitInfo, VK_NULL_HANDLE);

    VkSwapchainKHR swapchainHandle = m_swapchain.handle();
    VkPresentInfoKHR presentInfo
    {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderCompleteSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &swapchainHandle,
        .pImageIndices = &imageIndex,
        .pResults = nullptr
    };

    const VkResult presentResult = vkQueuePresentKHR(m_ctx.gfxQueue(), &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        m_swapchain.flagForRecreate();
    }
}