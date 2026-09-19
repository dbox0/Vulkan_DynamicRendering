#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include "../shaders/ShaderProgram.h"

class VulkanContext;

// Mirrors the push constant block shared by skybox.vert and skybox.frag.
struct SkyboxConstants
{
    glm::mat4 invViewProj{ 1.0f };   // offset 0
    glm::vec3 cameraPosition{ 0.0f };// offset 64
    uint32_t  cubeSlot = 0;          // offset 76. 1-based cube ID, 0 = no sky
    float     intensity = 1.0f;      // offset 80
    float     lod = 0.0f;            // offset 84. >0 samples the blurred mips
};

// Live-editable, separate from the bake settings in ibl/EnvironmentMap.h.
struct SkyboxSettings
{
    bool  enabled   = true;
    float intensity = 1.0f;
    float lod       = 0.0f;
};

//single fullscreen triangle
// view ray reconstructed by unprojecting the near plane.
// sky is emitted at fepth 0.0 -
//
// It samples cubes[] out of the bindless global set, so it needs no descriptor
// resources of its own; only a pipeline layout
class SkyboxPass
{
public:
    explicit SkyboxPass(VulkanContext &ctx) : m_ctx(ctx) {}
    SkyboxPass(const SkyboxPass &) = delete;
    SkyboxPass &operator=(const SkyboxPass &) = delete;

    // globalLayout is ResourceStore's bindless set layout, bound at set 0.
    bool createResources(VkDescriptorSetLayout globalLayout);

    void appendShaderPrograms(std::vector<ShaderProgram> &out);
    bool createPipelines();
    void destroy();

    void record(VkCommandBuffer cmd, VkDescriptorSet globalSet,
                const glm::mat4 &invViewProj, const glm::vec3 &cameraPosition,
                uint32_t cubeSlot) const;

    SkyboxSettings &settings() { return m_settings; }

private:
    VulkanContext &m_ctx;

    VkPipeline       m_pipeline       = nullptr;
    VkShaderModule   m_vertexShader   = nullptr;
    VkShaderModule   m_fragmentShader = nullptr;
    VkPipelineLayout m_layout         = nullptr;

    SkyboxSettings m_settings{};
};
