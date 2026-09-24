#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <array>
#include <functional>   // std::function, used by render()/recordCommandBuffer()
#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include "core/gpu_types.h"
#include "../scene/Scene.h"
#include "GpuShared.h"
#include "core/GPUProfiler.h"
#include "passes/debug/DebugLinePass.h"
#include "passes/ScenePass.h"
#include "passes/SelectionOutlinePass.h"
#include "shadows/ShadowPass.h"
#include "post/TonemapPass.h"
#include "shaders/ShaderProgram.h"
#include "shaders/ShaderWatcher.h"
#include "ibl/EnvironmentMap.h"
#include "passes/SkyboxPass.h"
#include "post/BloomPass.h"
#include "passes/ao/GtaoPass.h"
#include "../math/Frustum.h"
#include "culling/CullSettings.h"
#include "core/RenderTargets.h"
#include "passes/DepthPrepass.h"

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
    uint64_t itemsRevision     = UINT64_MAX;   // Scene::drawItemsRevision() last written
    uint64_t materialsRevision = UINT64_MAX;   // GeometryStore::materialRevision() last written
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
    static constexpr uint32_t ShadowResolution = 2048; // per Cascade
    static constexpr uint32_t CameraView = 0;
    static constexpr uint32_t ViewCount  = 1 + MaxShadowCascades;
    static constexpr uint32_t shadowView(uint32_t cascade) { return 1 + cascade; }


    Renderer(VulkanContext &ctx, Swapchain &swapchain,
             ResourceStore &resources, GeometryStore &geometry):
          m_ctx(ctx), m_swapchain(swapchain), m_targets(ctx) ,m_resources(resources), m_geometry(geometry),
          m_depthPrepass(ctx), m_scenePass(ctx), m_tonemapPass(ctx), m_shadowPass(ctx), m_outlinePass(ctx),
          m_debugLines(ctx), m_skyboxPass(ctx), m_bloomPass(ctx), m_gtao(ctx) {}

    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    bool initialize(uint32_t maxDrawItems);
    void syncRenderItems(FrameResources &res, const Scene &scene);
    void shutdown();

    void render(Scene &scene, const Camera &camera,
            uint32_t windowWidth, uint32_t windowHeight,
            const std::function<void(VkCommandBuffer)> &overlay = {});

    // Node ID the editor has selected, 0 for none.
    void setSelection(uint32_t nodeId) { m_selectedNode = nodeId; }

    void updateCullView(const glm::mat4 &view, const glm::mat4 &viewProj, float aspect);
    void collectCullDebugLines();
    CullSettings &cullSettings()      { return m_cull; }
    const CullStats &cullStats() const { return m_cullStats; }
    float drawListMs() const { return m_drawListMs; }



    // Colour and pixel width, live-editable from the inspector.
    OutlineConstants &outlineSettings() { return m_outlinePass.settings(); }

    // Bias and range knobs for the sun shadow, same idea.
    ShadowSettings &shadowSettings() { return m_shadow; }
    glm::vec3      &sunDirection()   { return m_sunDirection; }

    TonemapConstants &tonemapSettings() { return m_tonemapPass.settings(); }

    EnvironmentSettings &environmentSettings() { return m_environment; }
    SkyboxSettings      &skyboxSettings()      { return m_skyboxPass.settings(); }

    BloomSettings &bloomSettings() { return m_bloomPass.settings(); }
    GtaoSettings  &gtaoSettings()  { return m_gtao.settings(); }
    uint32_t bloomMipCount() const { return m_bloomPass.mipCount(); }




private:
    // The layout shared by the scene, shadow, mask and debug-line pipelines:
    // FrameConstants push constants, set 0 = bindless textures + materials,
    // set 1 = the shadow map. Bound once per frame, before any of them draw.
    bool createSceneLayout();

    bool createSyncResources();
    bool createCommandBuffers();
    bool createFrameBuffers(uint32_t maxDrawsPerFrame);

    bool bakeEnvironment(render::EnvironmentMap &out, std::vector<std::string> *shaderDeps,
                         std::string *shaderError);
    void bindEnvironment();
    void reloadEnvironment(const std::vector<std::string> &changedFiles);

    // Fills this frame's indirect + render-item buffers from the draw list.
    // Returns the draw count actually written (clamped to m_maxDraws).
    uint32_t writeDrawCommands(FrameResources &res, const glm::mat4 &viewProj);
    void recordCommandBuffer(FrameResources &res, uint32_t imageIndex, uint32_t drawCount,
                         const std::function<void(VkCommandBuffer)> &overlay = {});

    uint32_t lightMask(const glm::vec3 &lo, const glm::vec3 &hi) const;
    VulkanContext &m_ctx;
    Swapchain     &m_swapchain;
    ResourceStore &m_resources;
    GeometryStore &m_geometry;

    VkPipelineLayout m_sceneLayout = nullptr;
    DrawBatches      m_batches{};
    std::array<DrawBatches, MaxShadowCascades> m_shadowBatches{};
    std::array<std::array<std::vector<uint32_t>, ShadowBucketCount>, MaxShadowCascades> m_shadowLists;

    // --- passes -----------------------------------------------------------
    RenderTargets m_targets;

    DepthPrepass         m_depthPrepass;
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

    glm::mat4 m_lightBasis{ 1.0f };
    std::array<ShadowMap::ShadowCascade, MaxShadowCascades> m_cascades{};
    uint32_t  m_cascadeCount = 1;      // stays 1 until step 10
    uint32_t regionBase(uint32_t view) const { return view * m_itemCapacity; }

    EnvironmentSettings m_environment{};

    uint32_t m_selectedNode = 0;

    // Null in release builds.
    std::unique_ptr<ShaderWatcher> m_shaderWatcher;

    VkSemaphore m_timelineSemaphore = nullptr;
    std::array<FrameResources, MaxFramesInFlight> m_frameResources;
    uint64_t m_frameIndex       = 0;
    uint64_t m_nextSignalValue  = MaxFramesInFlight + 1;

    render::EnvironmentMap m_env;
    uint32_t m_envSourceTextureId = 0;
    std::vector<std::string> m_envShaderDeps;
    uint32_t m_envIrradianceSlot = 0;   // 1-based cube ID, 0 = none
    uint32_t m_envPrefilterSlot  = 0;
    uint32_t m_envSkyboxSlot     = 0;   // full-res sky; the prefilter cube is too small to look at
    float    m_envMaxLod         = 0.0f;


    uint32_t m_itemCapacity = 0;   // was m_maxDraws before
    uint32_t m_itemCount    = 0;   // min(drawItems.size(), capacity), set per frame

    // The skybox needs the inverse of the matrix render() already computed,
    // and recordCommandBuffer() runs too late to derive it from the camera.
    SkyboxPass m_skyboxPass;
    BloomPass m_bloomPass;
    GtaoPass  m_gtao;

    glm::mat4  m_invViewProj{ 1.0f };
    glm::vec3  m_cameraPosition{ 0.0f };

    Frustum m_cullFrustum{};
    glm::mat4 m_frozenViewProj{ 1.0f };
    glm::mat4    m_cullViewProj{ 1.0f };
    float m_frozenAspect = 1.0f;

    CullSettings m_cull{};
    CullStats m_cullStats{};
    bool m_wasClamped = false;

    float m_drawListMs = 0.0f;

    // Reused across frames so traversal doesn't allocate per frame.
    const std::vector<DrawItem> *m_drawItems = nullptr;
    std::vector<SortedDraw> m_sorted;

    GpuProfiler m_profiler;
};
