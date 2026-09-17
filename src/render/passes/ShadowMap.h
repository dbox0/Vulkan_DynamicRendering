#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include "../../common/gpu_types.h"
#include <cstdint>

class VulkanContext;
class Camera;

//Not GpuShared
struct ShadowSettings
{
    float distance     = 15.0;    // how far from the camera shadows are fitted
    float normalBias   = 3.0f;     // lookup offset along the normal, in texels
    float depthBias    = .0f;  // constant bias in light-space depth
    float constantBias = .0f;    // rasteriser depth bias, negated for reverse Z
    float slopeBias    = 2.0f;     // rasteriser slope-scaled bias, likewise
    bool  enabled      = true;
};

// directional shadow map: a depth image, a comparison sampler, and the
// descriptor set the scene pass binds at set 1.
//
// Fixed resolution

class ShadowMap
{
public:
    static constexpr VkFormat Format = VK_FORMAT_D32_SFLOAT;

    struct Fit
    {
        glm::mat4 lightViewProj{ 1.0f };
        float     worldTexelSize = 0.0f;   // world units one texel covers
    };

    explicit ShadowMap(VulkanContext &ctx) : m_ctx(ctx) {}
    ShadowMap(const ShadowMap &) = delete;
    ShadowMap &operator=(const ShadowMap &) = delete;

    bool create(uint32_t resolution);
    void destroy();

    // An ortho box around the camera frustum clipped at `distance`, snapped to
    // whole texels.
    // Without the snap:  projection shifts by a fraction of a
    // texel every time the camera moves and the shadow edges crawl.
    Fit fit(const Camera &camera, float aspect,
            const glm::vec3 &sunDirection, float distance) const;

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
