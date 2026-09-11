#pragma oince
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <glm/vec3.hpp>
#include <cstdint>

struct SDL_Window;
union SDL_Event;
class VulkanContext;
class Scene;
class GeometryStore;

// DearImgui Overlay: Scene Hierarchy on the left
// Transform inspector below.

// Holds NodeID.


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
    void build(Scene &scene, const GeometryStore &geometry);
    void record(VkCommandBuffer cmd);

    uint32_t selectedNode() const { return m_selectedNode; }
    void selectNode(uint32_t nodeId) { m_selectedNode = nodeId; }

private:
    void drawHierarchy(Scene &scene, const GeometryStore &geometry);
    void drawHierarchyNode(Scene &scene, const GeometryStore &geometry, uint32_t nodeId);
    void drawInspector(Scene &scene);

    VkDescriptorPool m_pool = nullptr;
    bool m_initialized = false;

    uint32_t m_selectedNode = 0;

    // Quaternions have no unique Euler decomposition, so round-tripping every
    // frame makes the sliders jitter and flip. Cache the Euler angles the
    // user is editing and only push them back into the quaternion on change.
    uint32_t  m_eulerOwner = 0;
    glm::vec3 m_eulerDegrees = glm::vec3(0.0f);
};
