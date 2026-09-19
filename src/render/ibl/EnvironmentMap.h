#pragma once
#include <volk.h>
#include <vk_mem_alloc.h>
#include <cstdint>
#include <string>
#include <vector>

#include "../core/Uploader.h"

namespace render {

inline constexpr VkFormat CubemapFORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;


struct Cubemap {
    VkImage       image      = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView   cubeView   = VK_NULL_HANDLE;
    std::vector<VkImageView> mipStorageViews;

    uint32_t size   = 0;
    uint32_t mips   = 1;
    VkFormat format = CubemapFORMAT;

    bool valid() const { return image != VK_NULL_HANDLE; }
};

struct BakeContext {
    VkDevice     device    = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    Uploader*    uploader  = nullptr;
};

struct EnvironmentSettings {
    uint32_t cubeSize       = 1024;
    uint32_t irradianceSize = 32;
    bool     generateMips   = false;
};

struct PrefilterPush {
    float    roughness;
    uint32_t mipSize;
    uint32_t sampleCount;
    float    sourceSize;
};

class EnvironmentMap {
public:
    EnvironmentMap() = default;
    ~EnvironmentMap();

    EnvironmentMap(const EnvironmentMap&)            = delete;
    EnvironmentMap& operator=(const EnvironmentMap&) = delete;
    EnvironmentMap(EnvironmentMap&&) noexcept;
    EnvironmentMap& operator=(EnvironmentMap&&) noexcept;

    bool load(const BakeContext& ctx,
              VkImageView equirectView,
              VkSampler   equirectSampler,
              const EnvironmentSettings& settings = {});

    void destroy();

    VkImageView skyboxView()     const { return m_skybox.cubeView; }
    VkImageView irradianceView() const { return m_irradiance.cubeView; }
    VkImageView prefilterView()  const { return m_skybox.cubeView; }
    VkSampler   sampler()        const { return m_sampler; }
    uint32_t    skyboxMips()     const { return m_skybox.mips; }
    bool        ready()          const { return m_skybox.valid(); }

private:
    bool createCubemap(Cubemap& out, uint32_t size, uint32_t mips);
    bool createSampler();
    bool createPipelines();

    void recordEquirectToCube(VkCommandBuffer cmd, VkImageView src, VkSampler srcSampler);
    void recordIrradiance(VkCommandBuffer cmd);
    void recordPrefilter(VkCommandBuffer cmd);

    void destroyBakeOnly();

    VkDescriptorSet allocateSet(VkDescriptorSetLayout layout);
    void destroyCubemap(Cubemap& c);

    BakeContext         m_ctx{};
    EnvironmentSettings m_settings{};

    Cubemap   m_skybox{};
    Cubemap   m_irradiance{};
    VkSampler m_sampler = VK_NULL_HANDLE;

    VkDescriptorPool       m_descriptorPool     = VK_NULL_HANDLE;
    VkDescriptorSetLayout  m_equirectSetLayout  = VK_NULL_HANDLE;
    VkDescriptorSetLayout  m_convolveSetLayout  = VK_NULL_HANDLE;
    VkPipelineLayout       m_equirectPipeLayout = VK_NULL_HANDLE;
    VkPipelineLayout       m_convolvePipeLayout = VK_NULL_HANDLE;
    VkPipeline             m_equirectPipeline   = VK_NULL_HANDLE;
    VkPipeline             m_irradiancePipeline = VK_NULL_HANDLE;


    VkDescriptorSetLayout m_prefilterSetLayout  = VK_NULL_HANDLE;
    VkPipelineLayout      m_prefilterPipeLayout = VK_NULL_HANDLE;
    VkPipeline            m_prefilterPipeline   = VK_NULL_HANDLE;
    VkImageView           m_skyboxMip0View      = VK_NULL_HANDLE;  // cube view, mip 0 only
};

}