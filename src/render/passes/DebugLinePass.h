#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include "../GpuShared.h"
#include "../shaders/ShaderProgram.h"

class VulkanContext;
class Scene;

// Editor overlay lines (light gizmos for now), collected on the CPU every
// frame and drawn as one line list inside the scene scope.
//
// Shares the renderer's scene layout: debug_line.vert reads the same push
// constant block, with the vertex address repointed at the line buffer.
class DebugLinePass
{
public:
    // Two vertices per line. Overlays only, so a few thousand is generous.
    static constexpr uint32_t MaxVertices = 8192;

    explicit DebugLinePass(VulkanContext &ctx) : m_ctx(ctx) {}
    DebugLinePass(const DebugLinePass &) = delete;
    DebugLinePass &operator=(const DebugLinePass &) = delete;

    void appendShaderPrograms(std::vector<ShaderProgram> &out, VkPipelineLayout layout);
    bool createPipelines(VkPipelineLayout layout);
    void destroy();

    // --- per frame ------------------------------------------------------
    void beginFrame();
    void addLine(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &color);
    void addBox(const glm::vec3 &lo, const glm::vec3 &hi, const glm::vec3 &color);
    uint32_t lineCount() const { return static_cast<uint32_t>(m_vertices.size() / 2); }
    
    // Line gizmo for every directional light in the scene: a handle at the
    // node and an arrow down its -Z
    void collectLightGizmos(Scene &scene);

    // Copies the collected lines into this frame's mapped buffer, clamped to
    // MaxVertices.
    void upload(DebugVertex *dst);

    // Inside the caller's rendering scope, over the geometry. Pushes its own
    // copy of the frame constants; nothing after it may rely on the vertex
    // address that was bound before.
    void record(VkCommandBuffer cmd, VkPipelineLayout layout,
                const FrameConstants &frameConsts, uint64_t lineBufferAddress) const;

private:
    VulkanContext &m_ctx;

    VkPipeline     m_pipeline       = nullptr;
    VkShaderModule m_vertexShader   = nullptr;
    VkShaderModule m_fragmentShader = nullptr;

    std::vector<DebugVertex> m_vertices;
    uint32_t                 m_vertexCount = 0;
};
