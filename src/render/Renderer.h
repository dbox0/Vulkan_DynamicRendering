#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <shaderc/shaderc.hpp>
#include <array>
#include <vector>
#include <string>
#include <cstdint>
#include "../common/gpu_types.h"
#include "../scene/Scene.h"
#include "GpuShared.h"

class VulkanContext;
class Swapchain;
class ResourceStore;
class GeometryStore;
class Camera;

struct DrawBatch
{
    uint32_t        first    = 0;
    uint32_t        count    = 0;
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    bool            blend    = false;
};

struct SortedDraw { uint32_t bucket; float depth; uint32_t index; };

struct FrameResources
{
    VkCommandPool   commandPool           = nullptr;
    VkCommandBuffer commandBuffer         = nullptr;
    VkSemaphore     imageAcquiredSemaphore = nullptr;
    GPUBuffer       indirectDrawBuffer;
    GPUBuffer       renderItemBuffer;
    GPUBuffer  frameDataBuffer;
    FrameData *frameDataPtr = nullptr;
    VkDrawIndexedIndirectCommand *indirectDrawPtr = nullptr;
    RenderItem                   *renderItemPtr   = nullptr;
};

class Renderer
{
public:
    static constexpr uint32_t MaxFramesInFlight = 2;

    Renderer(VulkanContext &ctx, Swapchain &swapchain,
             ResourceStore &resources, GeometryStore &geometry)
        : m_ctx(ctx), m_swapchain(swapchain), m_resources(resources), m_geometry(geometry) {}
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    bool initialize(uint32_t maxDrawsPerFrame);
    void shutdown();

    void render(Scene &scene, const Camera &camera,
            uint32_t windowWidth, uint32_t windowHeight,
            const std::function<void(VkCommandBuffer)> &overlay = {});

private:
    bool createShaders();
    bool createPipeline(bool blendEnabled, VkPipeline &outPipeline);
    bool createSyncResources();
    bool createCommandBuffers();
    bool createFrameBuffers(uint32_t maxDrawsPerFrame);
    VkShaderModule createShaderModule(const std::string &fileName, shaderc_shader_kind kind) const;

    // Fills this frame's indirect + render-item buffers from the draw list.
    // Returns the draw count actually written (clamped to m_maxDraws).
    uint32_t writeDrawCommands(FrameResources &res, const glm::mat4 &viewProj);
    void recordCommandBuffer(FrameResources &res, uint32_t imageIndex, uint32_t drawCount,
                         const std::function<void(VkCommandBuffer)> &overlay = {});
    VulkanContext &m_ctx;
    Swapchain     &m_swapchain;
    ResourceStore &m_resources;
    GeometryStore &m_geometry;

    VkPipelineLayout m_pipelineLayout = nullptr;
    VkPipeline m_pipelineOpaque = nullptr;
    VkPipeline m_pipelineBlend  = nullptr;
    std::array<DrawBatch, 4> m_batches{};

    VkShaderModule   m_vertexShader   = nullptr;
    VkShaderModule   m_fragmentShader = nullptr;

    VkSemaphore m_timelineSemaphore = nullptr;
    std::array<FrameResources, MaxFramesInFlight> m_frameResources;
    uint64_t m_frameIndex       = 0;
    uint64_t m_nextSignalValue  = MaxFramesInFlight + 1;
    uint32_t m_maxDraws         = 0;

    // Reused across frames so traversal doesn't allocate per frame.
    std::vector<DrawItem> m_drawItems;
    std::vector<SortedDraw> m_sorted;
};