#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include "../shaders/ShaderProgram.h"

class VulkanContext;

// Mirrors tonemap.frag's push constant block. Move into GpuShared.h once the
// layout settles, next to OutlineConstants.
enum class Tonemapper : uint32_t
{
    None     = 0,   // exposure only, clipped at 1.0 -- useful for comparison
    Reinhard = 1,
    ACES     = 2
};

struct TonemapConstants
{
    float      exposure   = 1.0f;
    Tonemapper tonemapper = Tonemapper::ACES;
    float      bloomStrength = 0.04f;
    int32_t    bloomDebugMip = -1;   // -1 = composite normally
};

// HDR scene colour -> display.
// Draws inside a rendering scope the renderer opens on the swapchain

// That scopehas the scene depth attached, hence the pipeline declares a depth format
// even though it never tests or writes depth.

// Like the outline composite, it needs a sampler the bindless global layout
// has no slot for, so it has a one-binding set and a layout of its own.
class TonemapPass
{
public:
    explicit TonemapPass(VulkanContext &ctx) : m_ctx(ctx) {}
    TonemapPass(const TonemapPass &) = delete;
    TonemapPass &operator=(const TonemapPass &) = delete;

    // Sampler, descriptor set and pipeline layout. None of them depend on a
    // shader, so hot reload never rebuilds them.
    bool createResources();

    void appendShaderPrograms(std::vector<ShaderProgram> &out);
    bool createPipelines();
    void destroy();

    // The HDR view changes identity on every swapchain recreate, so the
    // descriptor has to be rewritten with it.
    void setSourceView(VkImageView hdrView);
    void setBloomView(VkImageView bloomChainView);   // full chain, levels 0..N

    // Moves the HDR image COLOR_ATTACHMENT_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL.
    // Call after the scene scope ends, before the swapchain scope begins.
    void transitionSource(VkCommandBuffer cmd, VkImage hdrImage) const;

    // Inside the caller's swapchain scope. Sets its own viewport and scissor.
    void record(VkCommandBuffer cmd, VkExtent2D extent) const;

    // Live-editable from the editor.
    TonemapConstants &settings() { return m_settings; }

private:
    VulkanContext &m_ctx;

    VkPipeline            m_pipeline       = nullptr;
    VkShaderModule        m_vertexShader   = nullptr;
    VkShaderModule        m_fragmentShader = nullptr;
    VkPipelineLayout      m_layout         = nullptr;
    VkDescriptorSetLayout m_setLayout      = nullptr;
    VkDescriptorPool      m_pool           = nullptr;
    VkDescriptorSet       m_set            = nullptr;
    VkSampler             m_sampler        = nullptr;
    VkSampler             m_bloomSampler   = nullptr;

    TonemapConstants m_settings{};
};