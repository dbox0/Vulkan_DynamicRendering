#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include "../GpuShared.h"
#include "../shaders/ShaderProgram.h"

class VulkanContext;
struct SubMesh;

// Editor selection outline, in two steps:
//   mask      -- the selected draws rasterised into an R8 coverage image,
//                in a rendering scope of their own
//   composite -- a fullscreen triangle that dilates the mask, drawn inside
//                the caller's scene scope and scissored to the selection
//
// The mask pipeline shares the renderer's scene layout: it reads the same
// push constants and RenderItem buffer, so what is already bound covers it.
// The composite needs a sampler, which the bindless global layout has no
// free slot for, so it gets a one-binding layout of its own.
class SelectionOutlinePass
{
public:
    explicit SelectionOutlinePass(VulkanContext &ctx) : m_ctx(ctx) {}
    SelectionOutlinePass(const SelectionOutlinePass &) = delete;
    SelectionOutlinePass &operator=(const SelectionOutlinePass &) = delete;

    // Sampler, descriptor set and the composite's pipeline layout. None of
    // them depend on a shader, so hot reload never rebuilds them.
    bool createResources();

    void appendShaderPrograms(std::vector<ShaderProgram> &out, VkPipelineLayout sceneLayout);
    bool createPipelines(VkPipelineLayout sceneLayout);
    void destroy();

    // The mask view changes identity on every swapchain recreate, so the
    // descriptor has to be rewritten with it.
    void setMaskView(VkImageView maskView);

    // --- per frame ------------------------------------------------------
    // Fed while the draw list is written, after the sort: the mask pass
    // replays these exact slots out of the indirect buffer.
    void beginFrame();
    void addSelectedDraw(uint32_t slot, const glm::mat4 &viewProj,
                         const glm::mat4 &worldMatrix, const SubMesh &subMesh);
    bool hasSelection() const { return !m_selectedSlots.empty(); }

    // Draws the selected slots into the mask and leaves it in
    // SHADER_READ_ONLY_OPTIMAL. Must run before the scene scope begins.
    void recordMask(VkCommandBuffer cmd, VkBuffer indirectBuffer,
                    VkImage maskImage, VkImageView maskView, VkExtent2D extent) const;

    // Inside the caller's rendering scope, over the finished scene. Leaves a
    // narrowed scissor behind.
    void recordComposite(VkCommandBuffer cmd, VkExtent2D extent) const;

    // Colour and pixel width, live-editable from the inspector.
    OutlineConstants &settings() { return m_settings; }

private:
    bool createMaskPipeline(VkPipelineLayout sceneLayout);
    bool createCompositePipeline();

    // Screen-space box around the selection, padded by the outline width.
    // Scissoring the composite to this is what keeps a fullscreen dilate from
    // costing a fullscreen dilate.
    VkRect2D selectionScissor(VkExtent2D extent) const;

    VulkanContext &m_ctx;

    VkPipeline     m_pipelineMask       = nullptr;
    VkShaderModule m_maskVertexShader   = nullptr;
    VkShaderModule m_maskFragmentShader = nullptr;

    VkPipeline            m_pipelineOutline       = nullptr;
    VkShaderModule        m_outlineVertexShader   = nullptr;
    VkShaderModule        m_outlineFragmentShader = nullptr;
    VkPipelineLayout      m_outlineLayout         = nullptr;
    VkDescriptorSetLayout m_outlineSetLayout      = nullptr;
    VkDescriptorPool      m_outlinePool           = nullptr;
    VkDescriptorSet       m_outlineSet            = nullptr;
    VkSampler             m_maskSampler           = nullptr;

    OutlineConstants m_settings{};

    // Slots in this frame's indirect buffer that belong to the selection.
    std::vector<uint32_t> m_selectedSlots;
    glm::vec2 m_selectionMin{ 0.0f };
    glm::vec2 m_selectionMax{ 0.0f };
    bool      m_selectionBoundsValid = true;
};
