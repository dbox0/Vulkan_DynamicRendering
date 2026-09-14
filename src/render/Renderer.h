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

    // Node ID the editor has selected, 0 for none. The renderer never reaches
    // into EditorUI for it -- the application pushes it in once per frame, so
    // rendering stays independent of whether there is an editor at all.
    void setSelection(uint32_t nodeId) { m_selectedNode = nodeId; }

    // Colour and pixel width, live-editable from the inspector.
    OutlineConstants &outlineSettings() { return m_outline; }

private:
    bool createShaders();
    bool createPipeline(bool blendEnabled, VkPipeline &outPipeline);
    bool createMaskPipeline();
    bool createOutlinePipeline();
    bool createOutlineDescriptors();

    // The mask view changes identity on every swapchain recreate, so the
    // descriptor has to be rewritten with it.
    void updateSelectionMaskDescriptor();

    // Draws the selected submeshes into the mask image and leaves it in
    // SHADER_READ_ONLY_OPTIMAL.
    void recordSelectionMask(FrameResources &res);

    // Screen-space box around the selection, padded by the outline width.
    // Scissoring the composite to this is what keeps a fullscreen dilate from
    // costing a fullscreen dilate.
    VkRect2D selectionScissor() const;
    void accumulateSelectionBounds(const glm::mat4 &viewProj, const glm::mat4 &worldMatrix,
                                   const SubMesh &subMesh);


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

    // --- selection outline ------------------------------------------------
    // The mask pipeline shares m_pipelineLayout: it reads the same push
    // constants and the same RenderItem buffer, so the descriptor set and
    // constants bound for the scene pass already cover it.
    VkPipeline     m_pipelineMask         = nullptr;
    VkShaderModule m_maskVertexShader     = nullptr;
    VkShaderModule m_maskFragmentShader   = nullptr;

    // The composite needs a sampler, which the bindless global layout has no
    // free slot for, so it gets a one-binding layout of its own.
    VkPipeline            m_pipelineOutline    = nullptr;
    VkShaderModule        m_outlineVertexShader   = nullptr;
    VkShaderModule        m_outlineFragmentShader = nullptr;
    VkPipelineLayout      m_outlineLayout      = nullptr;
    VkDescriptorSetLayout m_outlineSetLayout   = nullptr;
    VkDescriptorPool      m_outlinePool        = nullptr;
    VkDescriptorSet       m_outlineSet         = nullptr;
    VkSampler             m_maskSampler        = nullptr;

    uint32_t         m_selectedNode = 0;
    OutlineConstants m_outline{};

    // Slots in this frame's indirect buffer that belong to the selection.
    std::vector<uint32_t> m_selectedSlots;
    glm::vec2 m_selectionMin{ 0.0f };
    glm::vec2 m_selectionMax{ 0.0f };
    bool      m_selectionBoundsValid = true;

    VkSemaphore m_timelineSemaphore = nullptr;
    std::array<FrameResources, MaxFramesInFlight> m_frameResources;
    uint64_t m_frameIndex       = 0;
    uint64_t m_nextSignalValue  = MaxFramesInFlight + 1;
    uint32_t m_maxDraws         = 0;

    uint32_t m_envSlot   = 0;
    float    m_envMaxLod = 0.0f;

    // Reused across frames so traversal doesn't allocate per frame.
    const std::vector<DrawItem> *m_drawItems = nullptr;
    std::vector<SortedDraw> m_sorted;
};