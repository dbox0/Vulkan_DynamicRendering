#include "Renderer.h"

#include <volk.h>
#include <vk_mem_alloc.h>
#include <algorithm>
#include <array>
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

    for (VkPipeline *p : { &m_pipelineOpaque, &m_pipelineBlend }) {
        if (*p) {
            vkDestroyPipeline(m_ctx.device(), *p, nullptr);
            *p = nullptr;
        }
    }

    if (m_pipelineLayout) {
        vkDestroyPipelineLayout(m_ctx.device(), m_pipelineLayout, nullptr);
        m_pipelineLayout = nullptr;
    }
    if (m_vertexShader) {
        vkDestroyShaderModule(m_ctx.device(), m_vertexShader, nullptr);
        m_vertexShader = nullptr;
    }
    if (m_fragmentShader) {
        vkDestroyShaderModule(m_ctx.device(), m_fragmentShader, nullptr);
        m_fragmentShader = nullptr;
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
    return m_fragmentShader != nullptr;
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
            .normalMatrix  = glm::transpose(glm::inverse(glm::mat3(item.worldMatrix))),
            .materialIndex = materialIndex
        };

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
            .srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            .srcAccessMask = 0,
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