#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <array>
#include <functional>   // std::function, used by render()/recordCommandBuffer()
#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include "../common/gpu_types.h"
#include "../scene/Scene.h"
#include "GpuShared.h"
#include "passes/DebugLinePass.h"
#include "passes/ScenePass.h"
#include "passes/SelectionOutlinePass.h"
#include "passes/ShadowPass.h"
#include "passes/TonemapPass.h"
#include "shaders/ShaderProgram.h"
#include "shaders/ShaderWatcher.h"
#include "ibl/EnvironmentMap.h"

class VulkanContext;
class Swapchain;
class ResourceStore;
class GeometryStore;
class Camera;

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

    GPUBuffer    debugLineBuffer;
    DebugVertex *debugLinePtr = nullptr;
};

struct RenderView
{
    const Camera *camera;
    VkImageView   colorTarget;      // per-view offscreen target
    VkImageView   depthTarget;
    VkExtent2D    extent;

    bool drawGrid           = false;
    bool drawSelectionOutline = false;
    bool drawGizmos         = false;
    bool drawDebugBounds    = false;

};


// Owns the frame: per-frame buffers, frame pacing, the draw list, and the
// order the passes run in.

// What each pass draws, and the pipelines it draws
// with, lives in render/passes.
struct EnvironmentSettings
{
    float ambientIntensity = .5f;
    float envIntensity     = 0.5f;
};

class Renderer
{
public:
    static constexpr uint32_t MaxFramesInFlight = 2;
    static constexpr uint32_t ShadowResolution   = 4096;

    Renderer(VulkanContext &ctx, Swapchain &swapchain,
             ResourceStore &resources, GeometryStore &geometry)
        : m_ctx(ctx), m_swapchain(swapchain), m_resources(resources), m_geometry(geometry),
          m_scenePass(ctx), m_tonemapPass(ctx), m_shadowPass(ctx), m_outlinePass(ctx), m_debugLines(ctx) {}
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    bool initialize(uint32_t maxDrawsPerFrame);
    void shutdown();

    void render(Scene &scene, const Camera &camera,
            uint32_t windowWidth, uint32_t windowHeight,
            const std::function<void(VkCommandBuffer)> &overlay = {});

    // Node ID the editor has selected, 0 for none. The renderer never reaches
    // into EditorUI for it -- the application pushes it in once per frame, so
    // rendering stays independent of whether there is an editor at all.
    void setSelection(uint32_t nodeId) { m_selectedNode = nodeId; }

    // Colour and pixel width, live-editable from the inspector.
    OutlineConstants &outlineSettings() { return m_outlinePass.settings(); }

    // Bias and range knobs for the sun shadow, same idea.
    ShadowSettings &shadowSettings() { return m_shadow; }
    glm::vec3      &sunDirection()   { return m_sunDirection; }

    TonemapConstants &tonemapSettings() { return m_tonemapPass.settings(); }

    EnvironmentSettings &environmentSettings() { return m_environment; }

private:
    // The layout shared by the scene, shadow, mask and debug-line pipelines:
    // FrameConstants push constants, set 0 = bindless textures + materials,
    // set 1 = the shadow map. Bound once per frame, before any of them draw.
    bool createSceneLayout();

    bool createSyncResources();
    bool createCommandBuffers();
    bool createFrameBuffers(uint32_t maxDrawsPerFrame);

    // Fills this frame's indirect + render-item buffers from the draw list.
    // Returns the draw count actually written (clamped to m_maxDraws).
    uint32_t writeDrawCommands(FrameResources &res, const glm::mat4 &viewProj);
    void recordCommandBuffer(FrameResources &res, uint32_t imageIndex, uint32_t drawCount,
                         const std::function<void(VkCommandBuffer)> &overlay = {});
    VulkanContext &m_ctx;
    Swapchain     &m_swapchain;
    ResourceStore &m_resources;
    GeometryStore &m_geometry;

    VkPipelineLayout m_sceneLayout = nullptr;
    DrawBatches      m_batches{};

    // --- passes -----------------------------------------------------------
    ScenePass            m_scenePass;
    ShadowPass           m_shadowPass;
    TonemapPass          m_tonemapPass;
    SelectionOutlinePass m_outlinePass;
    DebugLinePass        m_debugLines;

    // Every pass's shaders, for startup compilation and hot reload. Points
    // into the passes above, which live exactly as long as this does.
    std::vector<ShaderProgram> m_shaderPrograms;

    // --- sun shadow -------------------------------------------------------
    ShadowSettings m_shadow{};
    // m_shadow.enabled AND the active light's castsShadows, resolved per frame.
    bool           m_shadowActive = true;
    glm::vec3      m_sunDirection{ glm::normalize(glm::vec3(0.3f, -1.0f, -0.5f)) };

    EnvironmentSettings m_environment{};

    uint32_t m_selectedNode = 0;

    // Null in release builds.
    std::unique_ptr<ShaderWatcher> m_shaderWatcher;

    VkSemaphore m_timelineSemaphore = nullptr;
    std::array<FrameResources, MaxFramesInFlight> m_frameResources;
    uint64_t m_frameIndex       = 0;
    uint64_t m_nextSignalValue  = MaxFramesInFlight + 1;
    uint32_t m_maxDraws         = 0;

    render::EnvironmentMap m_env;
    uint32_t m_envIrradianceSlot = 0;   // 1-based cube ID, 0 = none
    uint32_t m_envPrefilterSlot  = 0;
    float    m_envMaxLod         = 0.0f;

    


    // Reused across frames so traversal doesn't allocate per frame.
    const std::vector<DrawItem> *m_drawItems = nullptr;
    std::vector<SortedDraw> m_sorted;
};
