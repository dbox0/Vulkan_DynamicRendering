#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include "../core/gpu_types.h"
#include <cstdint>

class VulkanContext;
class Camera;

//Not GpuShared
struct ShadowSettings
{
    float distance     = 15.0;    // how far from the camera shadows are fitted
    float normalBias   = .80f;     // lookup offset along the normal, in texels
    float depthBias    = .0f;  // constant bias in light-space depth
    float constantBias = .0f;    // rasteriser depth bias, negated for reverse Z
    float slopeBias    = 0.0f;     // rasteriser slope-scaled bias, likewise
    bool  enabled      = true;
};

class ShadowMap
{
public:
    static constexpr VkFormat Format = VK_FORMAT_D32_SFLOAT;

    struct ShadowCascade
    {
        glm::mat4 viewProj{ 1.0f };
        glm::vec2 lo{ 0.0f }, hi{ 0.0f };
        float     backDist   = 0.0f;       // sphere's far side along L
        float     radius     = 0.0f;
        float     worldTexel = 0.0f;       // world units per shadow texel
    };

    explicit ShadowMap(VulkanContext &ctx) : m_ctx(ctx) {}
    ShadowMap(const ShadowMap &) = delete;
    ShadowMap &operator=(const ShadowMap &) = delete;

    bool create(uint32_t resolution);
    void destroy();


    static glm::mat4 lightBasis(const glm::vec3 &sunDirection);

    ShadowCascade fitCascade(const Camera &camera, float aspect, const glm::mat4 &basis,
                         float sliceNear, float sliceFar) const;


    VkImage               image()           const { return m_target.image; }
    VkImageView           imageView()       const { return m_target.imageView; }
    VkDescriptorSet       descriptorSet()   const { return m_set; }
    VkDescriptorSetLayout descriptorLayout()const { return m_setLayout; }
    uint32_t              resolution()      const { return m_resolution; }

private:
    VulkanContext &m_ctx;

    uint32_t      m_resolution = 0;
    GPUImage      m_target;
    VkSampler     m_sampler    = nullptr;

    VkDescriptorSetLayout m_setLayout = nullptr;
    VkDescriptorPool      m_pool      = nullptr;
    VkDescriptorSet       m_set       = nullptr;
};
