#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <filesystem>
#include <vector>

#include "EditorCommands.h"
#include "EditRecorder.h"
#include "EditorSelection.h"
#include "../common/Guid.h"


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
class UndoHistory;


class EditorUI
{
public:
    bool initialize(SDL_Window *window, VulkanContext &ctx,
                    VkFormat colorFormat, VkFormat depthFormat,
                    uint32_t minImageCount, uint32_t imageCount,
                    VkQueue graphicsQueue, uint32_t graphicsQueueFamily);
    void shutdown(VulkanContext &ctx);


    void processEvent(const SDL_Event &event);
    bool wantsMouse() const;
    bool wantsKeyboard() const;

    // Per frame: beginFrame() -> build() -> (renderer records) -> record().
    void beginFrame();
    //
    // Everything is const: the editor reads the world and describes changes
    // as commands. A panel that writes to the scene directly would be an edit
    // undo never hears about, so it is made not to compile.

    void build(const Scene &scene, const GeometryStore &geometry, const ResourceStore &resources,
               const Camera &camera, uint32_t width, uint32_t height);
    void record(VkCommandBuffer cmd);

    // Edits recorded during build(), drained by Application afterwards. Nothing
    // in this class mutates the scene or the stores directly -- see
    // EditorCommands.h for why.
    const std::vector<EditorCommand> &commands() const { return m_commands; }
    void clearCommands() { m_commands.clear(); }

    bool gizmoCapturesMouse() const { return m_gizmoHovered; }

    // Hands the editor a pointer to the renderer's live shadow state so the
    // Shadows window can edit it in place. Call once after both exist.
    //
    // renderer settings, not scene data, so they are neither undoable nor saved with a
    // scene.

    // TODO: When they become per-scene (lighting settings), they become a
    // reflected object with an EditTarget kind

    void bindShadowSettings(ShadowSettings &settings, glm::vec3 &sunDirection)
    {
        m_shadowSettings = &settings;
        m_sunDirection   = &sunDirection;
    }

    // By Guid: the selection outlives frames, so it must not be a slot. A
    // selected node that dies simply stops resolving and the selection clears
    // itself on the next build().
    Guid selectedNode() const { return m_selectedNode; }

    // The whole selection as data, for undo to save and put back.
    EditorSelection selection() const;
    void setSelection(const EditorSelection &selection);

    /
    void bindHistory(const UndoHistory &history) { m_history = &history; }
    void selectNode(Guid node, uint32_t subMeshIndex = 0);
    void selectMaterial(uint32_t materialId);
    void clearSelection();



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


    enum class OriginFilter : uint8_t { All, Project, Imported };
    OriginFilter m_originFilter = OriginFilter::All;

    // ---- renaming --------------------------------------------------------
    // One rename at a time
    enum class RenameTarget : uint8_t { None, Node, Material, Asset };

    RenameTarget          m_renameTarget = RenameTarget::None;
    uint32_t              m_renameId     = 0;   // material ID
    Guid                  m_renameNode;         // node being renamed
    std::filesystem::path m_renamePath;         // the asset being renamed
    char                  m_renameBuffer[128]{};
    bool                  m_renameFocusPending = false;

    void beginRename(RenameTarget target, uint32_t id, const std::string &current);
    void beginRenameNode(Guid node, const std::string &current);
    void beginRenameAsset(const std::filesystem::path &path);
    void cancelRename();

    bool isRenaming(RenameTarget target, uint32_t id) const
    {
        return m_renameTarget == target && m_renameId == id;
    }
    bool isRenamingNode(Guid node) const
    {
        return m_renameTarget == RenameTarget::Node && m_renameNode == node;
    }
    bool isRenamingAsset(const std::filesystem::path &path) const
    {
        return m_renameTarget == RenameTarget::Asset && m_renamePath == path;
    }

    bool renameField(const char *id);

    // ---- save as ---------------------------------------------------------
    bool     m_saveAsRequested = false;
    uint32_t m_saveAsMaterial  = 0;
    char     m_saveAsBuffer[128]{};

    void drawSaveMaterialPopup(const ResourceStore &resources);

    // ---- the only ways out of this class ---------------------------------
    // Every command goes through submit(); every property change through
    // submitModify(), which groups changes into one edit per gesture
    void submit(EditorCommand command);
    void submitModify(const EditTarget &target, reflect::Blob snapshot, const char *name);

    // What the user is holding right now: ImGui's active widget, the gizmo,
    // or 0.
    EditRecorder::HeldId heldId() const;

    EditRecorder m_recorder;

    // Resolves m_selectedNode to this frame's slot, or clears a selection
    // whose node is gone. First thing build() does.
    void resolveSelection(const Scene &scene);

    // Which row or tile is highlighted in the Assets tab. A path rather than
    // an index, because the entry list is rebuilt every frame and an index
    // would silently point at a different file the moment a folder changes.
    std::filesystem::path m_selectedAsset;

    enum class ProjectView : uint8_t { List, Grid };
    ProjectView m_projectView   = ProjectView::Grid;
    float       m_thumbnailSize = 64.0f;

    // Rebuilt every frame from the current directory. ProjectItem::payload is
    // an index into this
    std::vector<std::filesystem::path> m_assetEntries;

    // We will initialize this in the cpp file
    std::filesystem::path m_currentAssetPath;

    void drawHierarchy(const Scene &scene, const GeometryStore &geometry);
    void drawHierarchyNode(const Scene &scene, const GeometryStore &geometry, uint32_t slot);
    void drawInspector(const Scene &scene, const GeometryStore &geometry, const ResourceStore &resources);

    // EditorInspectorPanels.cpp
    void drawMeshSection(Guid node, const Mesh &mesh, const ResourceStore &resources);

    // The inspector for a texture picked in the Textures tab: preview, format,
    // and which materials reference it.
    void drawTextureSection(const ResourceStore &resources, uint32_t textureId);

    // A null node means "this material is not attached to anything on
    // screen" (the Project panel's Materials tab) and suppresses the drop
    // target.
    void drawMaterialSection(const ResourceStore &resources, uint32_t materialId,
                             Guid node = {},
                             uint32_t subMesh = EditorCommand::kAllSubMeshes);

    // EditorInteraction.cpp
    void     beginMaterialDrag(const ResourceStore &resources, uint32_t materialId);
    uint32_t acceptMaterialDrop();
    void     assignMaterial(Guid node, uint32_t subMesh, uint32_t materialId);

    // A null parent creates at the scene root.
    void drawCreateMenuItems(Guid parent);

    void drawNodeContextMenu(Guid node, const std::string &name);
    void deleteNode(Guid node);

    void drawShadowWindow();

    // Colour / intensity / shadow toggle for a light node
    bool drawLightSection(Node &edited);

    const UndoHistory *m_history = nullptr;

    void drawEditMenu();
    void submitUndo();
    void submitRedo();

    ShadowSettings *m_shadowSettings   = nullptr;
    glm::vec3      *m_sunDirection     = nullptr;
    bool            m_showShadowWindow = false;

    void drawGizmo(const Scene &scene, const Camera &camera, uint32_t width, uint32_t height);
    void drawGizmoToolbar();
    void handleShortcuts(const Scene &scene, const ResourceStore &resources);

    void drawProjectPanel(const ResourceStore &resources);
    void drawAssetsTab();
    void drawMaterialsTab(const ResourceStore &resources);
    void drawTexturesTab(const ResourceStore &resources);


    bool passesOriginFilter(AssetOrigin origin) const;


    void drawAssetContextMenu();
    void drawMaterialsContextMenu();

    // EditorInteraction.cpp. Image files only
    void beginAssetDrag(const std::filesystem::path &path);

    // An already-loaded texture
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

    // The world matrix the gizmo is manipulating, held for the length of a
    // drag. ImGuizmo applies each frame's delta to the matrix it is given; if
    // that were re-read from the node, every frame would fold the node's
    // decompose/recompose rounding back in. Refreshed from the scene whenever
    // no drag is in progress.
    Guid      m_gizmoNode;
    glm::mat4 m_gizmoWorld{ 1.0f };

    void textureSlot(const char *label, TextureSlot slot, uint32_t materialId,
                     uint32_t textureId, const ResourceStore &resources);
    VkDescriptorSet texturePreview(const ResourceStore &resources, uint32_t textureId);

    VkDescriptorPool m_pool = nullptr;
    bool m_initialized = false;

    Guid     m_selectedNode;
    uint32_t m_selectedSlot = 0;   // this frame's slot for m_selectedNode, 0 if none

    // Which submesh of the selected node's mesh the material panel shows.
    // Reset whenever the node selection changes.
    Guid     m_subMeshOwner;
    size_t   m_selectedSubMesh = 0;

    // Texture ID -> ImGui descriptor set. ImGui's Vulkan backend can't read
    // the bindless array, so each texture we preview gets its own combined
    // image sampler set, created lazily on first draw and kept until
    // shutdown
    std::unordered_map<uint32_t, VkDescriptorSet> m_previewSets;

    // Quaternions have no unique Euler decomposition, so round-tripping every
    // frame makes the sliders jitter and flip. Cache the Euler angles the
    // user is editing and only push them back into the quaternion on change.

    // The cache is keyed on the quaternion it came from, not on an "owner"
    // flag: anything that rotates the node behind the inspector's back
    // changes the quaternion, and the cache notices by itself.

    Guid      m_eulerOwner;
    glm::quat m_eulerSource{ 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 m_eulerDegrees = glm::vec3(0.0f);
};