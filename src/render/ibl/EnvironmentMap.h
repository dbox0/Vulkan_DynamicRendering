#pragma once
#include <volk.h>
#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>
#include <vector>

static constexpr VkFormat EnvMapFORMAT = VK_FORMAT_R32G32B32A32_SFLOAT;

namespace render {
    struct Cubemap {
        VkImage         image      = VK_NULL_HANDLE;
        VmaAllocation   allocation = VK_NULL_HANDLE;
        VkImageView     cubeView   = VK_NULL_HANDLE;   // TYPE_CUBE, all mips (for sampling)
        std::vector<VkImageView> mipStorageViews;     // TYPE_2D_ARRAY, one mip each (for compute)

        uint32_t       size = 0;
        uint32_t       mips = 1;
        VkFormat       format = VK_FORMAT_R16G16B16A16_SFLOAT;

        bool valid() const { return image!=VK_NULL_HANDLE; }
    };

    struct BakeContext {
        VkDevice        device      = VK_NULL_HANDLE;
        VmaAllocator    allocator   = VK_NULL_HANDLE;
        VkQueue         queue       = VK_NULL_HANDLE;
        uint32_t        queueFamily = 0;
        VkCommandPool   commandPool = VK_NULL_HANDLE;   // transient command pool
    };

    struct EnvironmentSettings {
        uint32_t    cubeSize = 1024;
        uint32_t    irradianceSize = 32;
        uint32_t    generateMips = false; // true once GGX prefilter is added
    };

    class EnvironmentMap {

    public:
        EnvironmentMap() = default;
        ~EnvironmentMap();

        EnvironmentMap(const EnvironmentMap&) = delete;
        EnvironmentMap& operator=(const EnvironmentMap&) = delete;
        EnvironmentMap(EnvironmentMap&&) noexcept;
        EnvironmentMap& operator=(EnvironmentMap&&) noexcept;

        // Load .hdr, convert to cubemap, irradiance convolution then
        // releases the equirect.

        bool load(const BakeContext& bakeContext, const std::string& hrdPath, const EnvironmentSettings& settings = {});

        void destroy();

        VkImageView skyboxView()    const {return m_skybox.cubeView;}
        VkImageView irradiance()    const {return m_irradiance.cubeView;}
        VkSampler     sampler()     const {return m_sampler;}
        uint32_t    skyboxMips()    const {return m_skybox.mips;}
        bool        ready()         const {return m_skybox.valid();}


    private:
        bool loadEquirect(const std::string& path, VkCommandBuffer cmd);
        bool createCubemap(Cubemap& out, uint32_t size, uint32_t mips);
        bool createPipelines();
        void recordEquirectToCube(VkCommandBuffer cmd);
        void recordIrradiance(VkCommandBuffer cmd);
        void releaseStagingResources();

        void transition(VkCommandBuffer cmd,
                        VkImage img,
                        VkImageLayout oldLayout,
                        VkImageLayout newLayout,
                        const VkImageSubresourceRange& subresourceRange);

        VkDescriptorSet allocateSet(VkDescriptorSetLayout layout);

        BakeContext m_bakeContext;
        EnvironmentSettings m_settings{};

        Cubemap m_skybox{};
        Cubemap m_irradiance{};
        VkSampler m_sampler         = VK_NULL_HANDLE;
        VkSampler m_equirectSampler = VK_NULL_HANDLE;


        VkImage       m_equirect = VK_NULL_HANDLE;
        VmaAllocation m_equirectAlloc = VK_NULL_HANDLE;
        VkImageView   m_equirectView  = VK_NULL_HANDLE;
        VkBuffer      m_stagingBuff   = VK_NULL_HANDLE;
        VmaAllocation m_stagingAlloc  = VK_NULL_HANDLE;

        VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_irradianceSetLayout = VK_NULL_HANDLE;
        VkPipelineLayout      m_equirectLayout      = VK_NULL_HANDLE;
        VkPipelineLayout      m_convolveLayout      = VK_NULL_HANDLE;
        VkPipeline            m_equirectPipeline    = VK_NULL_HANDLE;
        VkPipeline            m_irradiancePipeline  = VK_NULL_HANDLE;
    };
}