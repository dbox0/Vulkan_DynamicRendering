#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <filesystem>
#include <vector>

#include "EditorCommands.h"


enum class AssetOrigin : uint8_t;
struct SDL_Window;
union SDL_Event;
class VulkanContext;
class Scene;
class Camera;
class GeometryStore;
class ResourceStore;
struct Mesh;
struct ShadowSettings;
class Node;

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
    void build(Scene &scene, const GeometryStore &geometry, ResourceStore &resources,
               const Camera &camera, uint32_t width, uint32_t height);
    void record(VkCommandBuffer cmd);

    // Edits recorded during build(), drained by Application afterwards. Nothing
    // in this class mutates the scene or the stores directly -- see
    // EditorCommands.h for why.
    const std::vector<EditorCommand> &commands() const { return m_commands; }
    void clearCommands() { m_commands.clear(); }

    // True while the cursor is over a gizmo handle, or a drag is in progress.
    // ImGuizmo draws into a NoInputs window, so io.WantCaptureMouse stays false
    // there and the viewport picker would happily deselect the node you were
    // about to grab.
    bool gizmoCapturesMouse() const { return m_gizmoHovered; }

    // Hands the editor a pointer to the renderer's live shadow state so the
    // Shadows window can edit it in place. Call once after both exist;

    void bindShadowSettings(ShadowSettings &settings, glm::vec3 &sunDirection)
    {
        m_shadowSettings = &settings;
        m_sunDirection   = &sunDirection;
    }

    uint32_t selectedNode() const { return m_selectedNode; }
    void selectNode(uint32_t nodeId, uint32_t subMeshIndex = 0);
    void selectMaterial(uint32_t materialId);
    void clearSelection();



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

    enum class SelectionMode { None, Node, Material, Texture };
    SelectionMode m_selectionMode = SelectionMode::None;

    uint32_t m_selectedMaterial = 0;
    uint32_t m_selectedTexture  = 0;

    enum class ProjectTab { Assets, Materials, Textures };
    ProjectTab m_projectTab = ProjectTab::Assets;

    // Applies to the Materials and Textures tabs. Builtin assets -- the white
    // and error textures, the engine default material -- only appear under
    // All: they are not something the user made or imported, and they would
    // otherwise be the first two tiles in every texture browser forever.
    enum class OriginFilter : uint8_t { All, Project, Imported };
    OriginFilter m_originFilter = OriginFilter::All;

    // ---- renaming --------------------------------------------------------
    // One rename at a time
    enum class RenameTarget : uint8_t { None, Node, Material, Asset };

    RenameTarget          m_renameTarget = RenameTarget::None;
    uint32_t              m_renameId     = 0;   // node or material ID
    std::filesystem::path m_renamePath;         // the asset being renamed
    char                  m_renameBuffer[128]{};
    bool                  m_renameFocusPending = false;

    void beginRename(RenameTarget target, uint32_t id, const std::string &current);
    void beginRenameAsset(const std::filesystem::path &path);
    void cancelRename();

    bool isRenaming(RenameTarget target, uint32_t id) const
    {
        return m_renameTarget == target && m_renameId == id;
    }
    bool isRenamingAsset(const std::filesystem::path &path) const
    {
        return m_renameTarget == RenameTarget::Asset && m_renamePath == path;
    }

    // The in-place edit field. Takes focus on its first frame, commits on
    // Enter or on clicking away, cancels on Escape. Returns true exactly once,
    // on commit, with the new text in m_renameBuffer.
    bool renameField(const char *id);

    // ---- save as ---------------------------------------------------------
    bool     m_saveAsRequested = false;
    uint32_t m_saveAsMaterial  = 0;
    char     m_saveAsBuffer[128]{};

    // Drawn from build(), not from the inspector: a modal has to be submitted
    // every frame it is open, and the inspector stops drawing the material the
    // moment the selection changes underneath it.
    void drawSaveMaterialPopup(const ResourceStore &resources);

    // Which row or tile is highlighted in the Assets tab. A path rather than
    // an index, because the entry list is rebuilt every frame and an index
    // would silently point at a different file the moment a folder changes.
    std::filesystem::path m_selectedAsset;

    // List vs grid for both Project tabs. Driven by m_thumbnailSize rather
    // than a separate toggle: dragging the zoom slider to its minimum IS list
    // view, which is one control instead of two.
    //
    // ProjectItem itself lives in EditorUI.cpp, not here. It holds an
    // ImTextureID and an ImVec4, and this header deliberately does not include
    // imgui.h -- same reason m_gizmoOperation is an int.
    enum class ProjectView : uint8_t { List, Grid };
    ProjectView m_projectView   = ProjectView::Grid;
    float       m_thumbnailSize = 64.0f;

    // Rebuilt every frame from the current directory. ProjectItem::payload is
    // an index into this, so the two must not get out of step.
    std::vector<std::filesystem::path> m_assetEntries;

    // We will initialize this in the cpp file
    std::filesystem::path m_currentAssetPath;

    void drawHierarchy(Scene &scene, const GeometryStore &geometry);
    void drawHierarchyNode(Scene &scene, const GeometryStore &geometry, uint32_t nodeId);
    void drawInspector(Scene &scene, const GeometryStore &geometry, ResourceStore &resources);

    // EditorInspectorPanels.cpp
    void drawMeshSection(uint32_t nodeId, const Mesh &mesh, const ResourceStore &resources);

    // The inspector for a texture picked in the Textures tab: preview, format,
    // and which materials reference it.
    void drawTextureSection(const ResourceStore &resources, uint32_t textureId);

    // nodeId 0 means "this material is not attached to anything on screen"
    // (the Project panel's Materials tab) and suppresses the drop target.
    void drawMaterialSection(ResourceStore &resources, uint32_t materialId,
                             uint32_t nodeId  = 0,
                             uint32_t subMesh = EditorCommand::kAllSubMeshes);

    // EditorInteraction.cpp
    void     beginMaterialDrag(const ResourceStore &resources, uint32_t materialId);
    uint32_t acceptMaterialDrop();
    void     assignMaterial(uint32_t nodeId, uint32_t subMesh, uint32_t materialId);

    void drawCreateMenuItems(uint32_t parentId);

    void drawNodeContextMenu(uint32_t nodeId, const std::string &name);
    void deleteNode(uint32_t nodeId);

    void drawShadowWindow();

    // Colour / intensity / shadow toggle for a light node. Direction is not
    // here: it is the node's rotation, edited with the transform or the gizmo.
    void drawLightSection(Node &node);

    ShadowSettings *m_shadowSettings   = nullptr;
    glm::vec3      *m_sunDirection     = nullptr;
    bool            m_showShadowWindow = false;

    void drawGizmo(Scene &scene, const Camera &camera, uint32_t width, uint32_t height);
    void drawGizmoToolbar();
    void handleShortcuts(Scene &scene, const ResourceStore &resources);

    void drawProjectPanel(ResourceStore &resources);
    void drawAssetsTab();
    void drawMaterialsTab(ResourceStore &resources);
    void drawTexturesTab(ResourceStore &resources);

    // True when an asset with this origin should be shown under the current
    // filter.
    bool passesOriginFilter(AssetOrigin origin) const;

    // Right-click on empty space in either tab.
    void drawAssetContextMenu();
    void drawMaterialsContextMenu();

    // EditorInteraction.cpp. Image files only: a drag that cannot be dropped
    // anywhere is worse than no drag at all.
    void beginAssetDrag(const std::filesystem::path &path);

    // An already-loaded texture, dragged out of the Textures tab. Distinct
    // from the path payload: this one needs no decode, no upload, and its
    // colour space is already fixed by whatever loaded it first.
    void beginTextureDrag(const ResourceStore &resources, uint32_t textureId);

    // Accepts either payload. Exactly one of outPath / outTextureId is filled.
    bool acceptTextureDrop(std::filesystem::path &outPath, uint32_t &outTextureId);

    // Returns true while a filter is active.
    bool searchBar(const char *id, std::string &filter);

    std::string m_assetFilter;
    std::string m_materialFilter;
    std::string m_textureFilter;
    std::vector<EditorCommand> m_commands;

    // Held as ints so this header does not have to pull in ImGuizmo, and
    // through it imgui.h. The values are ImGuizmo::TRANSLATE and
    // ImGuizmo::LOCAL.
    int   m_gizmoOperation = 7;
    int   m_gizmoMode      = 0;
    bool  m_gizmoSnap      = false;
    bool  m_gizmoHovered   = false;
    float m_snapTranslate  = 0.25f;
    float m_snapRotate     = 15.0f;
    float m_snapScale      = 0.1f;
    // The slot decides the expected colour space, so it is no longer a
    // separate argument that could disagree with it. materialId is needed
    // because the well is a drop target now.
    void textureSlot(const char *label, TextureSlot slot, uint32_t materialId,
                     uint32_t textureId, const ResourceStore &resources);
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