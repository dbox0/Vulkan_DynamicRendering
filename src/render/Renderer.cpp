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

namespace {
    VkDrawIndexedIndirectCommand makeCommand(const SubMesh &subMesh, uint32_t drawItemIndex)
    {
        return VkDrawIndexedIndirectCommand
        {
            .indexCount    = static_cast<uint32_t>(subMesh.indexCount),
            .instanceCount = 1,
            .firstIndex    = static_cast<uint32_t>(subMesh.indexStart),
            .vertexOffset  = static_cast<int32_t>(subMesh.vertexStart),
            .firstInstance = drawItemIndex,
        };
    }
}

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
    m_itemCapacity = maxDrawsPerFrame;

    if (!m_targets.create(m_swapchain.width(), m_swapchain.height())) {
        return false;
    }

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
    m_bloomPass.setSourceView(m_targets.hdrImageView());
    m_tonemapPass.setBloomView(m_bloomPass.resultView());

    if (!m_gtao.createResources() ||
        !m_gtao.createTargets(m_swapchain.width(), m_swapchain.height())) {
        return false;
    }
    m_gtao.setDepthView(m_targets.depthImageView());
    m_resources.setScreenAo(m_gtao.resultView(), m_gtao.sampler());

    m_depthPrepass.appendShaderPrograms(m_shaderPrograms, m_sceneLayout);
    m_scenePass.appendShaderPrograms(m_shaderPrograms, m_sceneLayout);
    m_tonemapPass.appendShaderPrograms(m_shaderPrograms);
    m_outlinePass.appendShaderPrograms(m_shaderPrograms, m_sceneLayout);
    m_shadowPass.appendShaderPrograms(m_shaderPrograms, m_sceneLayout);
    m_debugLines.appendShaderPrograms(m_shaderPrograms, m_sceneLayout);
    m_skyboxPass.appendShaderPrograms(m_shaderPrograms);
    m_bloomPass.appendShaderPrograms(m_shaderPrograms);
    m_gtao.appendShaderPrograms(m_shaderPrograms);

    if (!compileShaderPrograms(m_ctx.device(), m_shaderPrograms)) {
        showError("Error creating shader modules");
        return false;
    }

    if (!m_depthPrepass.createPipelines(m_sceneLayout)) {
        showError("Unable to initialize the depth prepass pipelines");
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
    if (!m_gtao.createPipelines()) {
        showError("Unable to initialize the GTAO pipelines");
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
    m_outlinePass.setMaskView(m_targets.selectionMaskImageView());
    m_tonemapPass.setSourceView(m_targets.hdrImageView());

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

    m_targets.destroy();

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
    m_depthPrepass.destroy();
    m_scenePass.destroy();
    m_tonemapPass.destroy();
    m_outlinePass.destroy();
    m_debugLines.destroy();
    m_shadowPass.destroy();
    m_bloomPass.destroy();
    m_gtao.destroy();
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

    const size_t indirectBytes = static_cast<size_t>(m_itemCapacity) * ViewCount
                           * sizeof(VkDrawIndexedIndirectCommand);

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
//==================== LightMask ======================================

uint32_t Renderer::lightMask(const glm::vec3 &lo, const glm::vec3 &hi) const
{
    const uint32_t all = (1u << m_cascadeCount) - 1u;
    if (!m_cull.enabled) {
        return all;         // "cull off" stays a true reference mode
    }

    const glm::vec3 c = glm::vec3(m_lightBasis * glm::vec4((lo + hi) * 0.5f, 1.0f));
    glm::mat3 A(m_lightBasis);
    for (int i = 0; i < 3; ++i) {
        A[i] = glm::abs(A[i]);
    }
    const glm::vec3 e = A * ((hi - lo) * 0.5f);
    const float nearestDist = -c.z - e.z;               // closest point along L

    uint32_t mask = 0;
    for (uint32_t k = 0; k < m_cascadeCount; ++k) {
        const ShadowMap::ShadowCascade &cs = m_cascades[k];
        const bool overlapsXY = c.x + e.x >= cs.lo.x && c.x - e.x <= cs.hi.x &&
                                c.y + e.y >= cs.lo.y && c.y - e.y <= cs.hi.y;
        if (overlapsXY && nearestDist <= cs.backDist) {
            mask |= 1u << k;
        }
    }
    return mask;
}




// ============================================================================
// draw recording
// ============================================================================

void Renderer::syncRenderItems(FrameResources &res, const Scene &scene)
{
    const uint64_t itemsRev    = scene.drawItemsRevision();
    const uint64_t materialRev = m_geometry.materialRevision();

    m_cullStats.renderItemBytes = 0;
    if (res.itemsRevision == itemsRev && res.materialsRevision == materialRev) {
        return;
    }

    const std::vector<DrawItem> &items = *m_drawItems;
    for (uint32_t i = 0; i < m_itemCount; ++i) {
        const SubMesh &subMesh = *items[i].subMesh;
        res.renderItemPtr[i] = RenderItem
        {
            .worldMatrix   = items[i].worldMatrix,
            .materialIndex = subMesh.materialId ? subMesh.materialId - 1 : 0,
        };
    }
    res.itemsRevision     = itemsRev;
    res.materialsRevision = materialRev;
    m_cullStats.renderItemBytes = m_itemCount * static_cast<uint32_t>(sizeof(RenderItem));
}

uint32_t Renderer::writeDrawCommands(FrameResources &res, const glm::mat4 &viewProj)
{
    resetBatches(m_batches);
    m_outlinePass.beginFrame();
    m_sorted.clear();
    for (uint32_t k = 0; k < m_cascadeCount; ++k) {
        for (std::vector<uint32_t> &list : m_shadowLists[k]) {
            list.clear();
        }
    }

    const std::vector<DrawItem> &items = *m_drawItems;

    // --- classify: one visit per item -------------------------------------
    for (uint32_t i = 0; i < m_itemCount; ++i) {
        const DrawItem  &item = items[i];
        const glm::vec3 &lo   = item.worldBoundsMin;
        const glm::vec3 &hi   = item.worldBoundsMax;

        const bool inCamera = !m_cull.enabled || m_cullFrustum.intersectsAABB(lo, hi);
        uint32_t   cascades = m_shadowActive ? lightMask(lo, hi) : 0u;
        if (!inCamera && cascades == 0) {
            continue;
        }

        const SubMesh  &subMesh    = *item.subMesh;
        const uint32_t  materialId = subMesh.materialId ? subMesh.materialId
                                                        : m_resources.defaultMaterialId();
        const Material &material   = m_resources.material(materialId);
        const DrawKind  kind = material.alphaMode == AlphaMode::Blend ? DrawKind::Blended
                             : material.alphaMode == AlphaMode::Mask  ? DrawKind::Masked
                                                                      : DrawKind::Opaque;
        const uint32_t bucket = drawBucket(kind, material.doubleSided);
        if (kind == DrawKind::Blended) {
            cascades = 0;
        }

        if (inCamera) {
            const glm::vec4 clip = viewProj * glm::vec4(glm::vec3(item.worldMatrix[3]), 1.0f);
            m_sorted.push_back(SortedDraw{ bucket, clip.w, i });
        }
        for (uint32_t bits = cascades; bits != 0; bits &= bits - 1) {
            m_shadowLists[std::countr_zero(bits)][bucket].push_back(i);   // <bit>
        }
    }

    // --- camera: sorted, region 0 ------------------------------------------
    std::stable_sort(m_sorted.begin(), m_sorted.end(),
    [](const SortedDraw &a, const SortedDraw &b)
        {
            if (a.bucket != b.bucket) return a.bucket < b.bucket;
            if (a.bucket < FirstBlendedBucket) return a.depth < b.depth;
            return a.depth > b.depth;                             // blended: far to near
        });

    const uint32_t drawCount = static_cast<uint32_t>(m_sorted.size());
    for (uint32_t slot = 0; slot < drawCount; ++slot) {
        const SortedDraw &sorted = m_sorted[slot];
        const DrawItem   &item   = items[sorted.index];

        res.indirectDrawPtr[slot] = makeCommand(*item.subMesh, sorted.index);

        if (m_selectedNode != 0 && item.nodeId == m_selectedNode) {
            m_outlinePass.addSelectedDraw(slot, viewProj, item.worldMatrix, *item.subMesh);
        }
        DrawBatch &batch = m_batches[sorted.bucket];
        if (batch.count == 0) {
            batch.first = slot;
        }
        ++batch.count;
    }

    // --- shadows: grouped by bucket, no sort, one region per cascade --------
    for (uint32_t k = 0; k < m_cascadeCount; ++k) {
        DrawBatches &batches = m_shadowBatches[k];
        resetBatches(batches);

        uint32_t slot = regionBase(shadowView(k));
        for (uint32_t b = 0; b < ShadowBucketCount; ++b) {
            const std::vector<uint32_t> &list = m_shadowLists[k][b];
            batches[b].first = slot;
            batches[b].count = static_cast<uint32_t>(list.size());
            for (const uint32_t index : list) {
                res.indirectDrawPtr[slot++] = makeCommand(*items[index].subMesh, index);
            }
        }
        m_cullStats.shadowCasters[k] = slot - regionBase(shadowView(k));
    }

    m_cullStats.total     = m_itemCount;
    m_cullStats.submitted = drawCount;
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
            .srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
                          | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .image = m_targets.depthImage(),
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
            .image = m_targets.hdrImage(),
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
        .imageView = m_targets.hdrImageView(),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue{ .color{ 0.3f, 0.3f, 1.0f, 1.0f } }
    };
    VkRenderingAttachmentInfo depthAttachInfo
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = m_targets.depthImageView(),
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_NONE
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

    {
        GpuScope prepassScope(m_profiler, res.commandBuffer, "Depth Prepass");
        m_depthPrepass.record(res.commandBuffer, res.indirectDrawBuffer.vkBuffer, m_batches,
                              m_targets.depthImageView(), extent, viewport);
    }

    // Prepass writes forward depth tests
    vkutil::imageBarrier(res.commandBuffer, {
        .image     = m_targets.depthImage(),
        .oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        .srcStage  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                     VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .srcAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstStage  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                     VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                     VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .range     = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
    });

    m_profiler.beginScope(res.commandBuffer,"Shadow");

    static constexpr const char *CascadeScopes[MaxShadowCascades] =
    { "Shadow C0", "Shadow C1", "Shadow C2", "Shadow C3" };

    m_shadowPass.beginCascades(res.commandBuffer);
    if (m_shadowActive) {
        for (uint32_t k = 0; k < m_cascadeCount; ++k) {
            GpuScope scope(m_profiler, res.commandBuffer, CascadeScopes[k]);
            m_shadowPass.recordCascade(res.commandBuffer, m_sceneLayout,
                                       res.indirectDrawBuffer.vkBuffer, k,
                                       m_shadowBatches[k], m_shadow);
        }
    }
    m_shadowPass.endCascades(res.commandBuffer);

    // Bound after the shadow pass to 1
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
    m_targets.selectionMaskImage(),
    m_targets.selectionMaskImageView(), extent);
    }
    m_profiler.endScope(res.commandBuffer);

    {
        //  __
        // / _\ ___ ___ _ __   ___
        // \ \ / __/ _ \ '_ \ / _ \
        // _\ \ (_|  __/ | | |  __/
        // \__/\___\___|_| |_|\___|
        //

    {
        GpuScope aoScope(m_profiler, res.commandBuffer, "GTAO");
        m_gtao.record(res.commandBuffer);


    vkCmdPushConstants(res.commandBuffer, m_sceneLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &push);


        m_gtao.makeResultReadable(res.commandBuffer);
    }
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
    m_tonemapPass.transitionSource(res.commandBuffer, m_targets.hdrImage());
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
        .imageView = m_targets.depthImageView(),
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_NONE
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
        if (!m_targets.recreate(m_swapchain.width(), m_swapchain.height())) {
            return;
        }
        m_tonemapPass.setSourceView(m_targets.hdrImageView());
        m_outlinePass.setMaskView(m_targets.selectionMaskImageView());
        m_bloomPass.destroyTargets();
        if (!m_bloomPass.createTargets(m_swapchain.width(), m_swapchain.height())) {
            return;
        }
        m_bloomPass.setSourceView(m_targets.hdrImageView());
        m_tonemapPass.setBloomView(m_bloomPass.resultView());

        m_gtao.destroyTargets();
        if (!m_gtao.createTargets(m_swapchain.width(), m_swapchain.height())) {
            return;
        }
        m_gtao.setDepthView(m_targets.depthImageView());
        m_resources.setScreenAo(m_gtao.resultView(), m_gtao.sampler());
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
    m_gtao.update(camera.projection(aspectRatio), camera.getViewMatrix(), viewProj);
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

    m_lightBasis  = ShadowMap::lightBasis(sunDirection);
    m_cascades[0] = m_shadowPass.map().fitCascade(camera, aspectRatio, m_lightBasis,
                                                  camera.nearClip(), m_shadow.distance);

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
        .lightViewProj    = m_cascades[0].viewProj,
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
        .shadowNormalBias = m_cascades[0].worldTexel * m_shadow.normalBias,
        .shadowDepthBias  = m_shadow.depthBias,
        .shadowEnabled    = m_shadowActive ? 1u : 0u,
    };


    m_drawItems = &scene.drawItems(m_geometry);

    const size_t available = m_drawItems->size();
    m_itemCount = static_cast<uint32_t>(std::min<size_t>(available, m_itemCapacity));
    const bool clamped = available > m_itemCapacity;
    if (clamped && !m_wasClamped) {
        std::cerr << "[warn] " << available << " draw items exceed capacity "
                  << m_itemCapacity << "; clamping\n";
    }
    m_wasClamped        = clamped;
    m_cullStats.clamped = clamped;

    syncRenderItems(res, scene);

    m_debugLines.beginFrame();
    m_debugLines.collectLightGizmos(scene);
    collectCullDebugLines();
    m_debugLines.upload(res.debugLinePtr);

    const auto t0 = std::chrono::steady_clock::now();
    const uint32_t drawCount = writeDrawCommands(res, viewProj);
    m_drawListMs = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - t0).count();

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
