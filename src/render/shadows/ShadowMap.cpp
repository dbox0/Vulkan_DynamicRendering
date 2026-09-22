#include "ShadowMap.h"

#include <volk.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

#include "../core/VulkanContext.h"
#include "../../common/errors.h"
#include "../../scene/Camera.h"

bool ShadowMap::create(uint32_t resolution)
{
    m_resolution = resolution;

    if (!m_ctx.createRenderTarget(resolution, resolution, Format,
                                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                  m_target)) {
        showError("Error creating the shadow map image");
        return false;
    }

    // A comparison sampler: each tap is a depth test, and LINEAR filters the
    // 0/1 results rather than the depths, so one lookup is already a 2x2 PCF.
    //
    // Reverse Z makes GREATER_OR_EQUAL the "lit" test, and an opaque black
    // border the "outside the map is lit" answer -- with the compare flipped,
    // a white border would shadow everything past the shadow distance.
    VkSamplerCreateInfo samplerInfo
    {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .compareEnable = VK_TRUE,
        .compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK
    };
    if (vkCreateSampler(m_ctx.device(), &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        showError("Error creating the shadow map sampler");
        return false;
    }

    VkDescriptorSetLayoutBinding binding
    {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding
    };
    if (vkCreateDescriptorSetLayout(m_ctx.device(), &layoutInfo, nullptr, &m_setLayout) != VK_SUCCESS) {
        showError("Error creating the shadow map descriptor set layout");
        return false;
    }

    VkDescriptorPoolSize poolSize
    {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1
    };
    VkDescriptorPoolCreateInfo poolInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize
    };
    if (vkCreateDescriptorPool(m_ctx.device(), &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        showError("Error creating the shadow map descriptor pool");
        return false;
    }

    VkDescriptorSetAllocateInfo setAllocInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &m_setLayout
    };
    if (vkAllocateDescriptorSets(m_ctx.device(), &setAllocInfo, &m_set) != VK_SUCCESS) {
        showError("Error allocating the shadow map descriptor set");
        return false;
    }
    VkDescriptorImageInfo descImageInfo
    {
        .sampler = m_sampler,
        .imageView = m_target.imageView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    VkWriteDescriptorSet write
    {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &descImageInfo
    };
    vkUpdateDescriptorSets(m_ctx.device(), 1, &write, 0, nullptr);
    return true;
}

void ShadowMap::destroy()
{
    if (!m_ctx.device()) {
        return;
    }
    if (m_pool) {                       // frees m_set with it
        vkDestroyDescriptorPool(m_ctx.device(), m_pool, nullptr);
        m_pool = nullptr;
        m_set  = nullptr;
    }
    if (m_setLayout) {
        vkDestroyDescriptorSetLayout(m_ctx.device(), m_setLayout, nullptr);
        m_setLayout = nullptr;
    }
    if (m_sampler) {
        vkDestroySampler(m_ctx.device(), m_sampler, nullptr);
        m_sampler = nullptr;
    }
    m_ctx.destroyImage(m_target);
}

ShadowMap::Fit ShadowMap::fit(const Camera &camera, float aspect,
                              const glm::vec3 &sunDirection, float distance) const
{
    // Clamped so a silly shadow distance cannot invert the slice and produce a
    // zero-radius sphere to divide by.
    const float far = std::clamp(distance, camera.nearClip() * 2.0f, camera.farClip());

    glm::vec3 corners[8];
    camera.frustumCornersWorld(aspect, camera.nearClip(), far, corners);

    glm::vec3 center{ 0.0f };
    for (const glm::vec3 &c : corners) {
        center += c;
    }
    center /= 8.0f;

    // A bounding sphere
    // rotating the camera cannot change how much world one texel
    // covers. A fitted box would resize every frame and shimmer.
    float radius = 0.0f;
    for (const glm::vec3 &c : corners) {
        radius = std::max(radius, glm::length(c - center));
    }
    radius = std::ceil(radius * 16.0f) / 16.0f;

    const glm::vec3 L  = glm::normalize(sunDirection);
    const glm::vec3 up = std::abs(L.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f)
                                               : glm::vec3(0.0f, 1.0f, 0.0f);

    // The eye sits 2r back so casters between the light and the sphere still
    // fall inside the depth range
    const glm::mat4 lightView = glm::lookAtRH(center - L * (radius * 2.0f), center, up);

    const float worldTexel = (2.0f * radius) / static_cast<float>(m_resolution);
    const glm::vec3 centerLS = glm::vec3(lightView * glm::vec4(center, 1.0f));
    const float snapX = std::floor(centerLS.x / worldTexel) * worldTexel;
    const float snapY = std::floor(centerLS.y / worldTexel) * worldTexel;

    // Near and far swapped == reverse Z, same trick the camera uses.
    const glm::mat4 lightProj = glm::orthoRH_ZO(
        snapX - radius, snapX + radius,
        snapY - radius, snapY + radius,
        radius * 4.0f, 0.0f);
    return Fit{ lightProj * lightView, worldTexel };
}
