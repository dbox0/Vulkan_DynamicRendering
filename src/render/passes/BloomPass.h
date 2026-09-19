// src/render/passes/BloomPass.h
#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <vector>
#include "../../common/gpu_types.h"
#include "../shaders/ShaderProgram.h"

class VulkanContext;

struct BloomSettings
{
    float threshold    = 1.0f;
    float softKnee     = 0.5f;
    float filterRadius = 0.005f;   // UV units, so the blur width survives a resize
    bool  enabled      = true;
};

// Mirrors the push block shared by bloom_down.comp and bloom_up.comp.
struct BloomConstants
{
    float    srcTexelSize[2]{};    // 1 / actual source extent, not dst * 2
    uint32_t dstSize[2]{};
    float    threshold    = 1.0f;
    float    softKnee     = 0.5f;
    float    filterRadius = 0.005f;
    uint32_t prefilter    = 0;     // 1 only for the HDR -> mip 0 dispatch
};

// Bloom mip chain: progressive compute downsample from the HDR target, then a
// tent-filtered upsample that adds each level back into the one above it.
// Mip 0 of the chain is half screen resolution; the tonemap pass samples it.

class BloomPass
{
public:
    static constexpr uint32_t MaxMips = 8;

    explicit BloomPass(VulkanContext &ctx) : m_ctx(ctx) {}
    BloomPass(const BloomPass &) = delete;
    BloomPass &operator=(const BloomPass &) = delete;

    bool createResources();

    // Chain image, per-mip views, and the per-dispatch descriptor sets.
    // Rebuilt on every swapchain recreate. Call setSourceView afterwards --
    // the mip 0 set references the HDR view, which has changed identity.
    bool createTargets(uint32_t width, uint32_t height);
    void destroyTargets();

    void appendShaderPrograms(std::vector<ShaderProgram> &out);
    bool createPipelines();
    void destroy();
    // The HDR target the chain reads from
    void setSourceView(VkImageView hdrView);

    // Outside any rendering scope. Leaves every level of the chain in
    // SHADER_READ_ONLY_OPTIMAL.
    void record(VkCommandBuffer cmd);

    // Levels 0..mipCount-1, for the tonemap pass. The composite reads level 0
    // and the editor's debug view reads any level, so this is the full chain
    // rather than a mip 0 view : sample with an explicit textureLod.
    VkImageView resultView() const { return m_chainFullView; }
    uint32_t    mipCount()   const { return m_mipCount; }

    BloomSettings &settings() { return m_settings; }

private:
    // Half resolution at level 0, halving until the minor axis reaches 16.
    void computeMipChain(uint32_t width, uint32_t height);

    bool createChainImage();
    bool createChainViews();
    bool allocateSets();

    // Writes the two image descriptors of one dispatch's set. src is sampled,
    // dst is a storage image.
    void writeSet(VkDescriptorSet set, VkImageView srcView, VkImageView dstView) const;

    // GENERAL -> GENERAL on the levels a dispatch touches: the previous
    // dispatch's storage writes have to be visible to this one's reads.
    void chainBarrier(VkCommandBuffer cmd, uint32_t baseMip, uint32_t mipCount,
                      bool dstReadsAndWrites) const;

    // Records one dispatch: binds the set, pushes constants sized for dstMip.
    void dispatchMip(VkCommandBuffer cmd, VkPipelineLayout layout,
                     VkDescriptorSet set, const BloomConstants &push,
                     VkExtent2D dstExtent) const;

    // Clears the chain to black and leaves it sampleable. The disabled path:
    // skipping the pass outright would leave the image UNDEFINED and make the
    // tonemap pass's sample undefined behaviour.
    void recordDisabled(VkCommandBuffer cmd) const;

    VulkanContext &m_ctx;

    GPUImage m_chain;
    uint32_t m_mipCount = 0;
    std::array<VkExtent2D, MaxMips> m_mipExtents{};

    // Separate view arrays because the two descriptor types have different requirements
    // also, a sampled view spanning several levels would make textureLod pick the wrong one!
    std::array<VkImageView, MaxMips> m_storageViews{};   // single level, imageStore
    std::array<VkImageView, MaxMips> m_sampleViews{};    // single level, texture()
    VkImageView m_chainFullView = nullptr;

    // One set per dispatch, allocated once per resize and rebound each frame.
    // m_downSets[0] is the HDR -> mip 0 prefilter; m_upSets[i] writes level i
    // from level i + 1. Last entry is unused
    std::array<VkDescriptorSet, MaxMips> m_downSets{};
    std::array<VkDescriptorSet, MaxMips> m_upSets{};

    VkPipeline     m_downPipeline = nullptr;
    VkPipeline     m_upPipeline   = nullptr;
    VkShaderModule m_downShader   = nullptr;
    VkShaderModule m_upShader     = nullptr;

    VkPipelineLayout      m_layout    = nullptr;
    VkDescriptorSetLayout m_setLayout = nullptr;
    VkDescriptorPool      m_pool      = nullptr;

    // LINEAR with CLAMP_TO_EDGE. NEAREST here turns the whole chain into
    // visible blocks; the wrap mode is what keeps the screen edges from
    // bleeding across.
    VkSampler m_sampler = nullptr;

    VkImageView m_hdrView = nullptr;   // not owned

    BloomSettings m_settings{};
};