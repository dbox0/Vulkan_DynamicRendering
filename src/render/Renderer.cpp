#include "Renderer.h"

#include <volk.h>
#include <vk_mem_alloc.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <utility>
#include <vector>

#include "core/Swapchain.h"
#include "core/VulkanContext.h"
#include "resources/GeometryStore.h"
#include "resources/ResourceStore.h"
#include "../common/errors.h"
#include "../common/constants.h"
#include <glm/glm.hpp>

#include "core/vkbarrier.h"
#include "../scene/Camera.h"

// ============================================================================
// lifetime
// ============================================================================
struct GpuScope
{
    GpuScope(GpuProfiler &p, VkCommandBuffer c, const char *n) : profiler(p), cmd(c)
    { profiler.beginScope(cmd, n); }
    ~GpuScope() { profiler.endScope(cmd); }
    GpuProfiler    &profiler;
    VkCommandBuffer cmd;
};

bool Renderer::initialize(uint32_t maxDrawsPerFrame)
{
    m_maxDraws = maxDrawsPerFrame;

    if (!m_tonemapPass.createResources()) {
        showError("Unable to initialize the tonemap resources");
        return false;
    }

    if (!m_shadowPass.createTarget(ShadowResolution)) {
        showError("Unable to create the shadow map");
        return false;
    }
    if (!createSceneLayout()) {
        return false;
    }
    if (!m_outlinePass.createResources()) {
        showError("Unable to initialize the selection outline resources");
        return false;
    }
    if (!m_skyboxPass.createResources(m_resources.globalLayout())) {
        showError("Unable to initialize the skybox resources");
        return false;
    }
    if (!m_bloomPass.createResources() ||
        !m_bloomPass.createTargets(m_swapchain.width(), m_swapchain.height())) {
        return false;
        }
    m_bloomPass.setSourceView(m_swapchain.hdrImageView());
    m_tonemapPass.setBloomView(m_bloomPass.resultView());


    m_scenePass.appendShaderPrograms(m_shaderPrograms, m_sceneLayout);
    m_tonemapPass.appendShaderPrograms(m_shaderPrograms);
    m_outlinePass.appendShaderPrograms(m_shaderPrograms, m_sceneLayout);
    m_shadowPass.appendShaderPrograms(m_shaderPrograms, m_sceneLayout);
    m_debugLines.appendShaderPrograms(m_shaderPrograms, m_sceneLayout);
    m_skyboxPass.appendShaderPrograms(m_shaderPrograms);
    m_bloomPass.appendShaderPrograms(m_shaderPrograms);

    if (!compileShaderPrograms(m_ctx.device(), m_shaderPrograms)) {
        showError("Error creating shader modules");
        return false;
    }

    if (!m_scenePass.createPipelines(m_sceneLayout)) {
        showError("Unable to initialize the graphics pipeline");
        return false;
    }

    if (!m_tonemapPass.createPipelines()) {
        showError("Unable to initialize the tonemap pipeline");
        return false;
    }

    if (!m_shadowPass.createPipelines(m_sceneLayout)) {
        showError("Unable to initialize the shadow pipeline");
        return false;
    }
    if (!m_debugLines.createPipelines(m_sceneLayout)) {
        showError("Unable to initialize the debug line pipeline");
        return false;
    }
    if (!m_outlinePass.createPipelines(m_sceneLayout)) {
        showError("Unable to initialize the selection outline pipelines");
        return false;
    }
    if (!m_skyboxPass.createPipelines()) {
        showError("Unable to initialize the skybox pipeline");
        return false;
    }
    if (!m_bloomPass.createPipelines()) {
        showError("Unable to initialize the bloom pipelines");
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
    m_envSourceTextureId = m_resources.loadEnvironment(ASSET_DIR "env/sky2k.hdr");
    if (m_envSourceTextureId && bakeEnvironment(m_env, &m_envShaderDeps, nullptr)) {
        bindEnvironment();
    }

    // The swapchain (and therefore the mask image) already exists by the time
    // the renderer is initialised, so the descriptor can be pointed at it now.
    m_outlinePass.setMaskView(m_swapchain.selectionMaskImageView());
    m_tonemapPass.setSourceView(m_swapchain.hdrImageView());

#ifndef NDEBUG
    m_shaderWatcher = std::make_unique<ShaderWatcher>(SHADER_DIR);
    m_profiler.initialize(m_ctx, MaxFramesInFlight);
#endif
    return true;
}

bool Renderer::bakeEnvironment(render::EnvironmentMap &out, std::vector<std::string> *shaderDeps,
                               std::string *shaderError)
{
    const auto& tex = m_resources.texture(m_envSourceTextureId);

    const render::BakeContext bake{
        .device    = m_ctx.device(),
        .allocator = m_ctx.allocator(),
        .uploader  = &m_ctx.uploader(),
    };
    const render::EnvironmentSettings settings{
        .cubeSize = 1024, .irradianceSize = 32, .generateMips = true,
    };

    m_ctx.uploader().wait(m_ctx.uploader().lastSubmitted());

    return out.load(bake, m_resources.imageView(tex.imageId), m_resources.sampler(tex.samplerId),
                    settings, shaderDeps, shaderError);
}

void Renderer::bindEnvironment()
{
    const auto bindCube = [this](uint32_t &slot, VkImageView view) {
        if (slot) {
            m_resources.setCubeTexture(slot, view, m_env.sampler());
        } else {
            slot = m_resources.addCubeTexture(view, m_env.sampler());
        }
    };
    bindCube(m_envIrradianceSlot, m_env.irradianceView());
    bindCube(m_envPrefilterSlot,  m_env.prefilterView());
    bindCube(m_envSkyboxSlot,     m_env.skyboxView());
    m_envMaxLod = static_cast<float>(m_env.prefilterMips() - 1);
    m_resources.setBrdfLut(m_env.brdfLutView(), m_env.sampler());
}

void Renderer::reloadEnvironment(const std::vector<std::string> &changedFiles)
{
    if (!m_envSourceTextureId || !dependsOnAny(m_envShaderDeps, changedFiles)) {
        return;
    }

    render::EnvironmentMap baked;
    std::vector<std::string> deps;
    std::string error;
    if (!bakeEnvironment(baked, &deps, &error)) {
        mergeDependencies(m_envShaderDeps, deps);
        std::cerr << "[hot reload] IBL bake failed, keeping the old environment:\n" << error << std::endl;
        return;
    }

    vkDeviceWaitIdle(m_ctx.device());
    m_env = std::move(baked);
    m_envShaderDeps = std::move(deps);
    bindEnvironment();
    std::cout << "[hot reload] rebaked IBL" << std::endl;
}

// Renderer.cpp
void Renderer::updateCullView(const glm::mat4 &view, const glm::mat4 &viewProj, float aspect)
{
    if (!m_cull.freeze) {
        m_frozenViewProj   = view;
        m_frozenAspect = aspect;
        m_cullViewProj = viewProj;
    } else if (m_cull.overrideProjection) {
        m_cullViewProj = glm::perspectiveRH_ZO(glm::radians(m_cull.fovDegrees),
                                               m_frozenAspect,
                                               m_cull.nearClip, m_cull.farClip) * m_frozenViewProj;
    }
    m_cullFrustum = Frustum::fromViewProj(m_cullViewProj);
}
void Renderer::shutdown()
{
    if (!m_ctx.device()) {
        return;
    }

    m_profiler.destroy();
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

        if (res.debugLineBuffer.allocation) {
            vmaUnmapMemory(m_ctx.allocator(), res.debugLineBuffer.allocation);
            res.debugLinePtr = nullptr;
        }
        m_ctx.destroyBuffer(res.debugLineBuffer);

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


    m_env.destroy();
    m_skyboxPass.destroy();
    m_scenePass.destroy();
    m_tonemapPass.destroy();
    m_outlinePass.destroy();
    m_debugLines.destroy();
    m_shadowPass.destroy();
    m_bloomPass.destroy();
    m_shaderPrograms.clear();


    if (m_sceneLayout) {
        vkDestroyPipelineLayout(m_ctx.device(), m_sceneLayout, nullptr);
        m_sceneLayout = nullptr;
    }
}

bool Renderer::createSceneLayout()
{
    VkPushConstantRange pushConstantRange
    {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(PushConstants)
    };

    // set 0: bindless textures + materials. set 1: the shadow map.
    const std::array<VkDescriptorSetLayout, 2> dsLayouts
    {
        m_resources.globalLayout(),
        m_shadowPass.map().descriptorLayout()
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast<uint32_t>(dsLayouts.size()),
        .pSetLayouts = dsLayouts.data(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange
    };

    if (vkCreatePipelineLayout(m_ctx.device(), &pipelineLayoutInfo, nullptr, &m_sceneLayout) != VK_SUCCESS) {
        showError("Failed to create the pipeline layout");
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

        // Editor overlays. Rewritten from scratch every frame, so per
        // frame-in-flight like the rest.
        if (!createMapped(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                          DebugLinePass::MaxVertices * sizeof(DebugVertex),
                          res.debugLineBuffer, ptr, "debug line buffer")) {
            return false;
        }
        res.debugLinePtr = static_cast<DebugVertex *>(ptr);
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

    m_outlinePass.beginFrame();

    const Frustum &cull = m_cullFrustum;

    const size_t itemCount = (*m_drawItems).size();

    m_sorted.clear();
    m_sorted.reserve(std::min<size_t>(itemCount, m_maxDraws));
    bool clamped = false;

    for (uint32_t i = 0; i < itemCount; ++i) {
        const DrawItem &item = (*m_drawItems)[i];

        if (m_cullEnabled && !cull.intersectsAABB(item.worldBoundsMin, item.worldBoundsMax)){
            continue;
        }
        if (m_sorted.size() >= m_maxDraws) {
           clamped = true;
           break;
       }
        const SubMesh &subMesh = *item.subMesh;
        const uint32_t materialId = subMesh.materialId ? subMesh.materialId
                                                     : m_resources.defaultMaterialId();
        const Material &material = m_resources.material(materialId);

        const uint32_t bucket = (material.alphaMode == AlphaMode::Blend ? 2u : 0u)
                              + (material.doubleSided ? 1u : 0u);

        const glm::vec4 clip = viewProj * glm::vec4(glm::vec3(item.worldMatrix[3]), 1.0f);
        m_sorted.push_back(SortedDraw{ bucket, clip.w, i });
    }
    const uint32_t drawCount = static_cast<uint32_t>(m_sorted.size());

    if (clamped) {
        std::cerr << "[warn] Visible draws exceed the per-frame limit of "
                  << m_maxDraws << "; clamping" << std::endl;
    }
    m_cullStats = { static_cast<uint32_t>(itemCount), drawCount };

    std::stable_sort(m_sorted.begin(), m_sorted.end(),
        [](const SortedDraw &a, const SortedDraw &b)
        {
            if (a.bucket != b.bucket) return a.bucket < b.bucket;
            if (a.bucket < 2)         return a.depth < b.depth;   // opaque: near to far
            return a.depth > b.depth;                             // blended: far to near
        });

    // Write slots
    for (uint32_t slot = 0; slot < drawCount; ++slot) {
        const SortedDraw &sorted = m_sorted[slot];
        const DrawItem &item = (*m_drawItems)[sorted.index];
        const SubMesh &subMesh = *item.subMesh;

        res.indirectDrawPtr[slot] = VkDrawIndexedIndirectCommand
        {
            .indexCount    = static_cast<uint32_t>(subMesh.indexCount),
            .instanceCount = 1,
            .firstIndex    = static_cast<uint32_t>(subMesh.indexStart),
            .vertexOffset  = static_cast<int32_t>(subMesh.vertexStart),
            .firstInstance = slot
        };

        const uint32_t materialIndex = subMesh.materialId ? subMesh.materialId - 1 : 0;

        res.renderItemPtr[slot] = RenderItem
        {
            .worldMatrix   = item.worldMatrix,
            .materialIndex = materialIndex
        };

        if (m_selectedNode != 0 && item.nodeId == m_selectedNode) {
            m_outlinePass.addSelectedDraw(slot, viewProj, item.worldMatrix, subMesh);
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
    m_profiler.beginFrame(res.commandBuffer);


    // UNDEFINED -> attachment layouts for colour and depth.
    const std::array<VkImageMemoryBarrier2, 3> layoutBarriers
    {
        // Swapchain Color
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

        //Depth
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
        },

        //HDR
        VkImageMemoryBarrier2
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
                            | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                            | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image = m_swapchain.hdrImage(),
            .subresourceRange
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1
            }
        }

    };
    VkDependencyInfo depInfo
    {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = static_cast<uint32_t>(layoutBarriers.size()),
        .pImageMemoryBarriers = layoutBarriers.data()
    };
    vkCmdPipelineBarrier2(res.commandBuffer, &depInfo);

    const VkExtent2D extent{ m_swapchain.width(), m_swapchain.height() };


    // Negative height flips Y so the glTF/glm convention lands right side up.
    VkViewport viewport
    {
        .x = 0,
        .y = static_cast<float>(extent.height),
        .width  =  static_cast<float>(extent.width),
        .height = -static_cast<float>(extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };

    vkCmdSetViewport(res.commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{ .offset{ .x = 0, .y = 0 }, .extent = extent };
    vkCmdSetScissor(res.commandBuffer, 0, 1, &scissor);


    VkRenderingAttachmentInfo colorAttachInfo
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = m_swapchain.hdrImageView(),
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
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue{ .depthStencil{ 0.0f, 0 } }     // reverse Z: 0 is the far plane
    };
    VkRenderingInfo renderingInfo
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea{ .offset{ .x = 0, .y = 0 }, .extent = extent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachInfo,
        .pDepthAttachment = &depthAttachInfo
    };

    // Frame-wide state: bindless descriptor set + buffer addresses.
    VkDescriptorSet globalSet = m_resources.globalDescriptorSet();
    vkCmdBindDescriptorSets(res.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_sceneLayout, 0, 1, &globalSet, 0, nullptr);


   const PushConstants push{.frameData = res.frameDataBuffer.deviceAddress};
    vkCmdPushConstants(res.commandBuffer, m_sceneLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &push);

    vkCmdBindIndexBuffer(res.commandBuffer, m_geometry.indexBuffer(), 0, VK_INDEX_TYPE_UINT32);



    // ======================== PASSES ============================ ///

    // Before everything else: the scene pass samples what it writes.

    m_profiler.beginScope(res.commandBuffer,"Shadow");

    m_shadowPass.record(res.commandBuffer, res.indirectDrawBuffer.vkBuffer, m_batches,
    m_shadow, m_shadowActive);

    // Bound after the shadow pass, not before
    VkDescriptorSet shadowSet = m_shadowPass.map().descriptorSet();
    vkCmdBindDescriptorSets(res.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
    m_sceneLayout, 1, 1, &shadowSet, 0, nullptr);

    m_profiler.endScope(res.commandBuffer);

    m_profiler.beginScope(res.commandBuffer,"Outline Pass");
    // Its own rendering scope, before the scene pass, so the composite inside
    // that pass can sample a finished mask. Skipped entirely when nothing is
    // selected: no barriers, no clear, no cost.
    const bool hasSelection = m_outlinePass.hasSelection();
    if (hasSelection) {m_outlinePass.recordMask(res.commandBuffer, res.indirectDrawBuffer.vkBuffer,
    m_swapchain.selectionMaskImage(),
    m_swapchain.selectionMaskImageView(), extent);
    }
    m_profiler.endScope(res.commandBuffer);

    {
        //  __
        // / _\ ___ ___ _ __   ___
        // \ \ / __/ _ \ '_ \ / _ \
        // _\ \ (_|  __/ | | |  __/
        // \__/\___\___|_| |_|\___|
        //


        GpuScope sceneScope(m_profiler,res.commandBuffer,"Scene");
        vkCmdBeginRendering(res.commandBuffer, &renderingInfo); // hdr color clear/store, depth clear/store
        {
            vkCmdSetViewport(res.commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(res.commandBuffer, 0, 1, &scissor);

            m_skyboxPass.record(res.commandBuffer, globalSet,
                                m_invViewProj, m_cameraPosition, m_envSkyboxSlot);

            vkCmdBindDescriptorSets(res.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_sceneLayout, 0, 1, &globalSet, 0, nullptr);
            vkCmdBindDescriptorSets(res.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_sceneLayout, 1, 1, &shadowSet, 0, nullptr);
            vkCmdPushConstants(res.commandBuffer, m_sceneLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(PushConstants), &push);

            m_scenePass.record(res.commandBuffer, res.indirectDrawBuffer.vkBuffer, m_batches);
        }
        vkCmdEndRendering(res.commandBuffer);
    }

    //  ====================  Tonemapping ======================

    m_profiler.beginScope(res.commandBuffer,"PostProcessing");
    m_tonemapPass.transitionSource(res.commandBuffer, m_swapchain.hdrImage());
    m_profiler.beginScope(res.commandBuffer,"Bloom");
    m_bloomPass.record(res.commandBuffer);
    m_profiler.endScope(res.commandBuffer);

    VkRenderingAttachmentInfo swapColorAttachInfo
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = m_swapchain.imageView(imageIndex),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE
    };
    VkRenderingAttachmentInfo swapDepthAttachInfo
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = m_swapchain.depthImageView(),
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE
    };
    VkRenderingInfo swapRenderingInfo
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea{ .offset{ .x = 0, .y = 0 }, .extent = extent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &swapColorAttachInfo,
        .pDepthAttachment = &swapDepthAttachInfo
    };

    // Test using our vkutil Image Barrier
    vkutil::imageBarrier(res.commandBuffer,
        {
            .image = m_swapchain.depthImage(),
            .oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .srcStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                        | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .srcAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .dstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                        | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .range = {VK_IMAGE_ASPECT_DEPTH_BIT , 0, 1 , 0 ,1}
        });


    vkCmdBeginRendering(res.commandBuffer, &swapRenderingInfo);
    {
        m_profiler.beginScope(res.commandBuffer,"Tonemap");
        m_tonemapPass.record(res.commandBuffer, extent);
        m_profiler.endScope(res.commandBuffer);

        // The tonemap pass left a positive-height viewport behind.
        vkCmdSetViewport(res.commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(res.commandBuffer, 0, 1, &scissor);

        m_profiler.beginScope(res.commandBuffer,"ImGui");
        m_debugLines.record(res.commandBuffer, m_sceneLayout, push);

        if (hasSelection) {
            m_outlinePass.recordComposite(res.commandBuffer, extent);
        }

        if (overlay) {
            overlay(res.commandBuffer);
        }
        m_profiler.endScope(res.commandBuffer);
    }
    vkCmdEndRendering(res.commandBuffer);
    m_profiler.endScope(res.commandBuffer); // Postprocessing


    // COLOR_ATTACHMENT -> PRESENT_SRC.
    VkImageMemoryBarrier2 presentLayoutBarrier
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
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
void Renderer::collectCullDebugLines()
{
    m_cullStats.boxesDrawn = 0;

    if (m_cull.drawFrustum && m_cull.freeze) {
        glm::vec3 c[8];
        Frustum::cornersWorld(m_cullViewProj, c);

        static constexpr int edges[12][2] = {
            {0,1},{1,3},{3,2},{2,0},
            {4,5},{5,7},{7,6},{6,4},
            {0,4},{1,5},{2,6},{3,7}
        };
        const glm::vec3 color{ 1.0f, 0.85f, 0.2f };
        for (const auto &e : edges) {
            m_debugLines.addLine(c[e[0]], c[e[1]], color);
        }
    }

    if (!m_cull.drawVisible && !m_cull.drawCulled) {
        return;
    }

    static constexpr glm::vec3 kVisible{ 0.25f, 0.9f, 0.35f };
    static constexpr glm::vec3 kCulled { 0.95f, 0.25f, 0.25f };

    for (const DrawItem &item : *m_drawItems) {
        if (m_cullStats.boxesDrawn >= static_cast<uint32_t>(m_cull.boxBudget)) {
            break;
        }
        const bool visible = m_cullFrustum.intersectsAABB(item.worldBoundsMin, item.worldBoundsMax);
        if (visible ? !m_cull.drawVisible : !m_cull.drawCulled) {
            continue;
        }
        m_debugLines.addBox(item.worldBoundsMin, item.worldBoundsMax,
                            visible ? kVisible : kCulled);
        ++m_cullStats.boxesDrawn;
    }
}

void Renderer::render(Scene &scene, const Camera &camera, uint32_t windowWidth, uint32_t windowHeight,const std::function<void(VkCommandBuffer)> &overlay)
{
    if (m_swapchain.needsRecreate()) {
        if (!m_swapchain.recreate(windowWidth, windowHeight)) {
            return;
        }
        // New image, new view: the outline's descriptor still points at the
        // destroyed one. recreate() waits idle, so rewriting here is safe.
        m_tonemapPass.setSourceView(m_swapchain.hdrImageView());
        m_outlinePass.setMaskView(m_swapchain.selectionMaskImageView());
        m_bloomPass.destroyTargets();
        if (!m_bloomPass.createTargets(m_swapchain.width(), m_swapchain.height())) {
            return;
        }
        m_bloomPass.setSourceView(m_swapchain.hdrImageView());
        m_tonemapPass.setBloomView(m_bloomPass.resultView());
    }

    // Between frames and before anything is recorded, so a rebuilt pipeline
    // is used by this very frame.
    if (m_shaderWatcher) {
        const std::vector<std::string> changed = m_shaderWatcher->poll();
        reloadShaderPrograms(m_ctx.device(), m_shaderPrograms, changed);
        reloadEnvironment(changed);
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

    m_ctx.uploader().poll();
    m_geometry.tick(m_frameIndex);
    
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

    // recordCommandBuffer() reconstructs view rays from these.
    m_invViewProj    = glm::inverse(viewProj);
    m_cameraPosition = camera.position;
    updateCullView(camera.getViewMatrix(), viewProj, aspectRatio);

    // A Directional Light node in the scene drives the sun. Without one the
    // renderer's own m_sunDirection is the fallback, so a scene that has never
    // had a light in it is still lit.
    glm::vec3 sunDirection = glm::normalize(m_sunDirection);
    glm::vec3 sunColor     = glm::vec3(1.0f, 0.96f, 0.9f);
    float     sunIntensity = 3.0f;
    m_shadowActive = m_shadow.enabled;

    if (const uint32_t lightNode = scene.firstDirectionalLight()) {
        const Node &node = scene.nodes().getNode(lightNode);
        const glm::vec3 forward = -glm::vec3(scene.nodes().worldMatrix(lightNode)[2]);

        if (glm::length(forward) > 1e-6f) {
            sunDirection = glm::normalize(forward);
        }
        sunColor       = node.lightColor;
        sunIntensity   = node.lightIntensity;
        m_shadowActive = m_shadow.enabled && node.lightCastsShadows;
    }

    // Refitted every frame: the box follows the camera, so what it covers is
    // the near slice of the view rather than the whole world.
    const ShadowMap::Fit shadow =
        m_shadowPass.map().fit(camera, aspectRatio, sunDirection, m_shadow.distance);

    *res.frameDataPtr = FrameData
    {
        .table = {
            .positions   = m_geometry.positionBufferAddress(),
            .attributes  = m_geometry.attributeBufferAddress(),
            .colors      = m_geometry.colorBufferAddress(),
            .materials   = m_resources.materialBufferAddress(),
            .renderItems = res.renderItemBuffer.deviceAddress,
            .debugLines  = res.debugLineBuffer.deviceAddress,
        },
        .viewProj       = viewProj,
        .lightViewProj  = shadow.lightViewProj,
        .cameraPosition = camera.position,
        .exposure       = 1.0f,
        .sunDirection   = sunDirection,
        .sunIntensity   = sunIntensity,
        .sunColor       = sunColor,
        .ambientIntensity = m_environment.ambientIntensity,
        .envIrradianceTex = m_envIrradianceSlot,
        .envPrefilterTex  = m_envPrefilterSlot,
        .envIntensity     = m_environment.envIntensity,
        .envMaxLod        = m_envMaxLod,
        .shadowTexelSize  = 1.0f / static_cast<float>(m_shadowPass.map().resolution()),
        .shadowNormalBias = shadow.worldTexelSize * m_shadow.normalBias,
        .shadowDepthBias  = m_shadow.depthBias,
        .shadowEnabled    = m_shadowActive ? 1u : 0u,
    };


    m_drawItems = &scene.drawItems(m_geometry);

    m_debugLines.beginFrame();
    m_debugLines.collectLightGizmos(scene);
    collectCullDebugLines();
    m_debugLines.upload(res.debugLinePtr);

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
