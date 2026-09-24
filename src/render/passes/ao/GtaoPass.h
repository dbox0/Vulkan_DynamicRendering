#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include "../../core/gpu_types.h"
#include "../../shaders/ShaderProgram.h"

class VulkanContext;

struct GtaoSettings {
    bool enabled        = true;
    int  quality        = 1;
    int  denoisePasses  = 1;
    float radius        = 0.5f;
    float falloffRange      = 0.615f;
    float finalPower        = 2.2f;
    float mipSamplingOffset = 3.3f;
};

// Push Block. Shared by  gtao_*.comp
struct GtaoConstants
{
    glm::ivec2 viewportSize{ 0 };
    glm::vec2  viewportPixelSize{ 0.0f };
    glm::vec2  depthUnpack{ 0.0f };
    glm::vec2  uvToViewMul{ 0.0f };
    glm::vec2  uvToViewAdd{ 0.0f };
    glm::vec2  uvToViewMulPixel{ 0.0f };
    float      effectRadius      = 0.0f;
    float      falloffRange      = 0.0f;
    float      finalPower        = 0.0f;
    float      denoiseBeta       = 0.0f;
    float      mipSamplingOffset = 0.0f;
    uint32_t   sliceCount        = 0;
    uint32_t   stepsPerSlice     = 0;
    uint32_t   noiseIndex        = 0;
    uint32_t   finalPass         = 0;
};

static_assert(sizeof(GtaoConstants) == 84);
static_assert(offsetof(GtaoConstants, effectRadius) == 48);
static_assert(offsetof(GtaoConstants, sliceCount)   == 68);


class GtaoPass {
public:
    static constexpr uint32_t DepthMips          = 5;
    static constexpr VkFormat WorkingDepthFormat = VK_FORMAT_R32_SFLOAT;
    static constexpr VkFormat AoFormat           = VK_FORMAT_R8_UNORM;

    explicit GtaoPass(VulkanContext &ctx) : m_ctx(ctx) {}
    GtaoPass(const GtaoPass &) = delete;
    GtaoPass &operator=(const GtaoPass &) = delete;

    bool createResources();
    bool createTargets(uint32_t width, uint32_t height);
    void destroyTargets();
    void setDepthView(VkImageView depthView);

    void appendShaderPrograms(std::vector<ShaderProgram> &out);
    bool createPipelines();
    void destroy();

    // one per frame before record
    void update(const glm::mat4 &projection);

    void makeResultReadable(VkCommandBuffer cmd) const;

    void record(VkCommandBuffer cmd);
    VkImageView   resultView() const { return m_final.imageView; }
    VkSampler     sampler()    const { return m_pointSampler; }
    GtaoSettings &settings()         { return m_settings; }

private:
    enum Stage : uint32_t { Prefilter, Main, Denoise, StageCount };

    // Denoise ping-pong. Src alternates working/temp
    enum DenoiseSet : uint32_t { WorkingToTemp, TempToWorking, WorkingToFinal, TempToFinal, DenoiseSetCount };

    bool createWorkingDepth();
    bool allocateSets();
    void writeImage(VkDescriptorSet set, uint32_t binding, uint32_t element,
                    VkDescriptorType type, VkImageView view, VkImageLayout layout) const;
    bool createPipeline(Stage stage, VkPipeline &outPipeline);
    void dispatch(VkCommandBuffer cmd, Stage stage, VkDescriptorSet set,
                  uint32_t groupsX, uint32_t groupsY) const;
    void recordDisabled(VkCommandBuffer cmd) const;   // result cleared to white

    VulkanContext &m_ctx;

    uint32_t m_width  = 0;
    uint32_t m_height = 0;

    GPUImage m_workingDepth;                              // imageView = all mips (sampled)
    std::array<VkImageView, DepthMips> m_depthMipViews{}; // one level each
    GPUImage m_aoWorking;
    GPUImage m_aoTemp;
    GPUImage m_final;
    GPUImage m_edges;

    VkSampler m_pointSampler = nullptr;

    std::array<VkDescriptorSetLayout, StageCount> m_setLayouts{};
    std::array<VkPipelineLayout, StageCount>      m_layouts{};
    std::array<VkPipeline, StageCount>            m_pipelines{};
    std::array<VkShaderModule, StageCount>        m_shaders{};
    VkDescriptorPool m_pool = nullptr;

    VkDescriptorSet m_prefilterSet = nullptr;
    VkDescriptorSet m_mainSet      = nullptr;
    std::array<VkDescriptorSet, DenoiseSetCount> m_denoiseSets{};

    GtaoConstants m_constants{};
    GtaoSettings  m_settings{};
};