#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

struct SDL_Window;
union SDL_Event;
class VulkanContext;
class Scene;
class GeometryStore;
class ResourceStore;
struct Mesh;

// Dear ImGui overlay:
//   * "Hierarchy" window on the left  -- click a node to select it
//   * "Inspector" window on the right -- transform, mesh / submeshes, and the
//     material of the selected submesh with texture previews

class EditorUI
{
public:
    bool initialize(SDL_Window *window, VulkanContext &ctx,
                    VkFormat colorFormat, VkFormat depthFormat,
                    uint32_t minImageCount, uint32_t imageCount,
                    VkQueue graphicsQueue, uint32_t graphicsQueueFamily);
    void shutdown(VulkanContext &ctx);

    // Call for every SDL event, BEFORE your own handling. Check
    // wantsMouse()/wantsKeyboard() afterwards so camera controls do not fire
    // while the user is dragging a slider.
    void processEvent(const SDL_Event &event);
    bool wantsMouse() const;
    bool wantsKeyboard() const;

    // Per frame: beginFrame() -> build() -> (renderer records) -> record().
    void beginFrame();
    void build(Scene &scene, const GeometryStore &geometry, ResourceStore &resources);
    void record(VkCommandBuffer cmd);

    uint32_t selectedNode() const { return m_selectedNode; }
    void selectNode(uint32_t nodeId) { m_selectedNode = nodeId; }

    void selectNode(uint32_t nodeId, uint32_t subMesh)
    {
        m_selectedNode    = nodeId;
        m_subMeshOwner    = nodeId;
        m_selectedSubMesh = subMesh;
    }

    // Drop a cached preview. Call this if a texture's image/sampler is ever
    // swapped (ResourceStore::replaceTextureDescriptor), behind the same
    // frames-in-flight rule, since ImGui may still be sampling the old set.
    void invalidateTexturePreview(uint32_t textureId);

    void applyTheme();
    static bool vec3Control(const char *label, glm::vec3 &values,
                            float resetValue = 0.0f, float speed = 0.05f);
    static void beginProperties(const char *id);
    static void endProperties();

private:
    void drawHierarchy(Scene &scene, const GeometryStore &geometry);
    void drawHierarchyNode(Scene &scene, const GeometryStore &geometry, uint32_t nodeId);
    void drawInspector(Scene &scene, const GeometryStore &geometry, ResourceStore &resources);

    // EditorInspectorPanels.cpp
    void drawMeshSection(const Mesh &mesh, const ResourceStore &resources);
    void drawMaterialSection(ResourceStore &resources, uint32_t materialId);
    void textureSlot(const char *label, uint32_t textureId, bool expectSrgb,
                     const ResourceStore &resources);
    VkDescriptorSet texturePreview(const ResourceStore &resources, uint32_t textureId);

    VkDescriptorPool m_pool = nullptr;
    bool m_initialized = false;

    uint32_t m_selectedNode = 0;

    // Which submesh of the selected node's mesh the material panel shows.
    // Reset whenever the node selection changes.
    uint32_t m_subMeshOwner    = 0;
    size_t   m_selectedSubMesh = 0;

    // Texture ID -> ImGui descriptor set. ImGui's Vulkan backend can't read
    // the bindless array, so each texture we preview gets its own combined
    // image sampler set, created lazily on first draw and kept until
    // shutdown (the pool is sized so this never runs out).
    std::unordered_map<uint32_t, VkDescriptorSet> m_previewSets;

    // Quaternions have no unique Euler decomposition, so round-tripping every
    // frame makes the sliders jitter and flip. Cache the Euler angles the
    // user is editing and only push them back into the quaternion on change.
    uint32_t  m_eulerOwner = 0;
    glm::vec3 m_eulerDegrees = glm::vec3(0.0f);
};