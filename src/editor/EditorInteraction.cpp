// ============================================================================
// EditorInteraction.cpp
//
// The half of EditorUI that produces edits rather than displays state:
// the transform gizmo, material drag and drop, and the create / delete menus.
// Split out so EditorUI.cpp stays about lifetime, theme and panel layout.
//
// Nothing here mutates the scene directly. Everything goes on the command
// queue and is applied by Application after build() returns -- see
// EditorCommands.h for why.
// ============================================================================

#include "EditorUI.h"
#include "EditorCommands.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>

#include <cstdio>
#include <string>

#include "../scene/Scene.h"
#include "../scene/Camera.h"
#include "../render/GeometryStore.h"
#include "../render/ResourceStore.h"

namespace
{
    // One payload type for materials, so every target accepts every source.
    constexpr const char *kMaterialPayload = "EDITOR_MATERIAL";

    // A filesystem path, as bytes including the terminator. ImGui copies the
    // payload into its own buffer, so a pointer into a temporary would be
    // fine -- but the string itself has to be the payload, not a pointer to
    // it, because the source's storage is gone by the time the drop lands.
    constexpr const char *kAssetPathPayload = "EDITOR_ASSET_PATH";

    // An already-resident texture. Kept separate from the path payload
    // because the two resolve differently: a path has to be decoded in the
    // colour space the destination slot wants, while a texture that already
    // exists has its format baked in and gets reused as-is.
    constexpr const char *kTexturePayload = "EDITOR_TEXTURE";
}

void EditorUI::beginAssetDrag(const std::filesystem::path &path)
{
    if (!ImGui::BeginDragDropSource()) {
        return;
    }

    const std::string text = path.string();
    ImGui::SetDragDropPayload(kAssetPathPayload, text.c_str(), text.size() + 1);

    // Filename rather than the full path: the preview follows the cursor and a
    // long absolute path covers the target you are aiming at.
    ImGui::TextUnformatted(path.filename().string().c_str());
    ImGui::EndDragDropSource();
}

void EditorUI::beginTextureDrag(const ResourceStore &resources, uint32_t textureId)
{
    if (!ImGui::BeginDragDropSource()) {
        return;
    }

    ImGui::SetDragDropPayload(kTexturePayload, &textureId, sizeof(uint32_t));

    if (const VkDescriptorSet set = texturePreview(resources, textureId)) {
        ImGui::Image(reinterpret_cast<ImTextureID>(set), ImVec2(32.0f, 32.0f));
        ImGui::SameLine();
    }

    const ResourceStore::ImageInfo &info =
        resources.imageInfo(resources.texture(textureId).imageId);
    ImGui::TextUnformatted(info.name.empty() ? "(unnamed image)" : info.name.c_str());

    ImGui::EndDragDropSource();
}

bool EditorUI::acceptTextureDrop(std::filesystem::path &outPath, uint32_t &outTextureId)
{
    if (!ImGui::BeginDragDropTarget()) {
        return false;
    }

    bool accepted = false;

    // Texture first: if both somehow matched, the resident one is the cheaper
    // and less surprising answer.
    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kTexturePayload)) {
        outTextureId = *static_cast<const uint32_t *>(payload->Data);
        accepted     = true;
    } else if (const ImGuiPayload *pathPayload = ImGui::AcceptDragDropPayload(kAssetPathPayload)) {
        outPath  = std::filesystem::path(static_cast<const char *>(pathPayload->Data));
        accepted = true;
    }

    ImGui::EndDragDropTarget();
    return accepted;
}

// ---------------------------------------------------------------------------
// drag and drop
// ---------------------------------------------------------------------------

void EditorUI::beginMaterialDrag(const ResourceStore &resources, uint32_t materialId)
{
    // Default flags: the source is the item drawn immediately before this, and
    // the drag only starts once the mouse actually moves, so a plain click
    // still selects.
    if (!ImGui::BeginDragDropSource()) {
        return;
    }

    ImGui::SetDragDropPayload(kMaterialPayload, &materialId, sizeof(uint32_t));

    // The preview is what makes the drop target legible -- without it you are
    // dragging an unlabelled rectangle across three panels.
    const Material &mat = resources.material(materialId);
    const std::string name = mat.name.empty() ? "Material " + std::to_string(materialId)
                                              : mat.name;

    if (mat.baseColorTexture != 0 && mat.baseColorTexture <= resources.textureCount()) {
        if (const VkDescriptorSet set = texturePreview(resources, mat.baseColorTexture)) {
            ImGui::Image(reinterpret_cast<ImTextureID>(set), ImVec2(32.0f, 32.0f));
            ImGui::SameLine();
        }
    } else {
        ImGui::ColorButton("##dragcolor",
                           ImVec4(mat.baseColorFactor.r, mat.baseColorFactor.g,
                                  mat.baseColorFactor.b, 1.0f),
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                           ImVec2(32.0f, 32.0f));
        ImGui::SameLine();
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(name.c_str());

    ImGui::EndDragDropSource();
}

uint32_t EditorUI::acceptMaterialDrop()
{
    uint32_t materialId = 0;

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kMaterialPayload)) {
            // Copy rather than alias: ImGui owns the payload buffer and reuses
            // it, and this value outlives the frame on the command queue.
            materialId = *static_cast<const uint32_t *>(payload->Data);
        }
        ImGui::EndDragDropTarget();
    }
    return materialId;
}

void EditorUI::assignMaterial(uint32_t nodeId, uint32_t subMesh, uint32_t materialId)
{
    if (nodeId == 0 || materialId == 0) {
        return;
    }
    EditorCommand cmd;
    cmd.kind       = EditorCommand::Kind::AssignMaterial;
    cmd.nodeId     = nodeId;
    cmd.subMesh    = subMesh;
    cmd.materialId = materialId;
    m_commands.push_back(cmd);
}

// ---------------------------------------------------------------------------
// create / delete
// ---------------------------------------------------------------------------

// Shared by the Hierarchy toolbar, the empty-space context menu and the
// per-node context menu. parentId 0 creates at the scene root.
void EditorUI::drawCreateMenuItems(uint32_t parentId)
{
    if (ImGui::MenuItem("Empty Node")) {
        EditorCommand cmd;
        cmd.kind     = EditorCommand::Kind::CreateEmpty;
        cmd.parentId = parentId;
        m_commands.push_back(cmd);
    }

    ImGui::Separator();

    for (uint8_t i = 0; i < static_cast<uint8_t>(PrimitiveType::Count); ++i) {
        const auto type = static_cast<PrimitiveType>(i);
        if (ImGui::MenuItem(primitiveName(type))) {
            EditorCommand cmd;
            cmd.kind      = EditorCommand::Kind::CreatePrimitive;
            cmd.primitive = type;
            cmd.parentId  = parentId;
            m_commands.push_back(cmd);
        }
    }
}

void EditorUI::deleteNode(uint32_t nodeId)
{
    if (nodeId == 0) {
        return;
    }
    EditorCommand cmd;
    cmd.kind   = EditorCommand::Kind::DeleteNode;
    cmd.nodeId = nodeId;
    m_commands.push_back(cmd);

    // Dropped here rather than in Application: the selection is this class's
    // state, and leaving it pointing at a node that dies later this frame
    // means one frame of the inspector reading a dead slot.
    if (m_selectedNode == nodeId) {
        clearSelection();
    }
}


// ---------------------------------------------------------------------------
// renaming
// ---------------------------------------------------------------------------

void EditorUI::beginRename(RenameTarget target, uint32_t id, const std::string &current)
{
    m_renameTarget = target;
    m_renameId     = id;
    m_renamePath.clear();
    std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", current.c_str());
    m_renameFocusPending = true;
}

void EditorUI::beginRenameAsset(const std::filesystem::path &path)
{
    m_renameTarget = RenameTarget::Asset;
    m_renameId     = 0;
    m_renamePath   = path;

    // Stem, not filename: nobody wants to retype ".mat", and typing over the
    // extension by accident is the classic way to make a file disappear from
    // its own browser.
    std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s",
                  path.stem().string().c_str());
    m_renameFocusPending = true;
}

void EditorUI::cancelRename()
{
    m_renameTarget = RenameTarget::None;
    m_renameId     = 0;
    m_renamePath.clear();
    m_renameFocusPending = false;
}

bool EditorUI::renameField(const char *id)
{
    if (m_renameFocusPending) {
        ImGui::SetKeyboardFocusHere();
        m_renameFocusPending = false;
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool entered = ImGui::InputText(id, m_renameBuffer, sizeof(m_renameBuffer),
                                          ImGuiInputTextFlags_EnterReturnsTrue |
                                          ImGuiInputTextFlags_AutoSelectAll);

    // Enter commits. So does clicking away
    // Escape: cancel

    if (entered || ImGui::IsItemDeactivatedAfterEdit()) {
        return true;
    }
    if (ImGui::IsItemDeactivated()) {
        cancelRename();
    }
    return false;
}

void EditorUI::drawSaveMaterialPopup(const ResourceStore &resources)
{
    constexpr const char *popupId = "Save Material As";

    if (m_saveAsRequested) {
        ImGui::OpenPopup(popupId);
        m_saveAsRequested = false;
    }

    if (!ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    if (m_saveAsMaterial == 0 || m_saveAsMaterial > resources.materialCount()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    // The folder is wherever the Assets tab is pointing, shown rather than
    // chosen.
    // A real directory picker is a lot of UI for a decision the user
    // has usually already made by browsing there.

    ImGui::TextDisabled("Folder");
    ImGui::TextUnformatted(m_currentAssetPath.string().c_str());
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    ImGui::TextDisabled("Name");
    ImGui::SetNextItemWidth(280.0f);
    const bool entered = ImGui::InputText("##saveasname", m_saveAsBuffer, sizeof(m_saveAsBuffer),
                                          ImGuiInputTextFlags_EnterReturnsTrue);

    const bool named = m_saveAsBuffer[0] != '\0';

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::BeginDisabled(!named);
    const bool confirmed = ImGui::Button("Save", ImVec2(90.0f, 0.0f)) || (entered && named);
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }

    if (confirmed) {
        EditorCommand cmd;
        cmd.kind       = EditorCommand::Kind::SaveMaterial;
        cmd.materialId = m_saveAsMaterial;
        cmd.path       = m_currentAssetPath / (std::string(m_saveAsBuffer) + ".mat");
        m_commands.push_back(cmd);
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void EditorUI::drawNodeContextMenu(uint32_t nodeId, const std::string &name)
{
    if (!ImGui::BeginPopupContextItem()) {
        return;
    }

    if (ImGui::BeginMenu("Create Child")) {
        drawCreateMenuItems(nodeId);
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Rename", "F2")) {
        beginRename(RenameTarget::Node, nodeId, name);
    }

    if (ImGui::MenuItem("Duplicate")) {
        EditorCommand cmd;
        cmd.kind   = EditorCommand::Kind::DuplicateNode;
        cmd.nodeId = nodeId;
        m_commands.push_back(cmd);
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Delete", "Del")) {
        deleteNode(nodeId);
    }

    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// gizmo
// ---------------------------------------------------------------------------

void EditorUI::drawGizmoToolbar()
{
    // Radio buttons rather than a combo: the mode has to be readable at a
    // glance while you are mid-drag.
    const auto modeButton = [this](const char *label, int op, const char *shortcut)
    {
        const bool selected = m_gizmoOperation == op;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        }
        if (ImGui::Button(label)) {
            m_gizmoOperation = op;
        }
        if (selected) {
            ImGui::PopStyleColor();
        }
        if (ImGui::BeginItemTooltip()) {
            ImGui::Text("%s (%s)", label, shortcut);
            ImGui::EndTooltip();
        }
        ImGui::SameLine();
    };

    modeButton("Move",   ImGuizmo::TRANSLATE, "1");
    modeButton("Rotate", ImGuizmo::ROTATE,    "2");
    modeButton("Scale",  ImGuizmo::SCALE,     "3");

    // Scaling in world space is meaningless for a rotated node -- ImGuizmo
    // ignores MODE for SCALE anyway, so say so instead of offering a toggle
    // that does nothing.
    ImGui::BeginDisabled(m_gizmoOperation == ImGuizmo::SCALE);
    if (ImGui::Button(m_gizmoMode == ImGuizmo::WORLD ? "World" : "Local")) {
        m_gizmoMode = (m_gizmoMode == ImGuizmo::WORLD) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    }
    ImGui::EndDisabled();
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Gizmo space (X)");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    ImGui::Checkbox("Snap", &m_gizmoSnap);
    if (m_gizmoSnap) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);

        // One snap value per operation: 0.25 units is useless for degrees and
        // 15 degrees is useless for metres.
        float *value = m_gizmoOperation == ImGuizmo::TRANSLATE ? &m_snapTranslate
                     : m_gizmoOperation == ImGuizmo::ROTATE    ? &m_snapRotate
                                                               : &m_snapScale;
        ImGui::DragFloat("##snap", value, 0.05f, 0.001f, 180.0f, "%.3f");
    }
}

void EditorUI::handleShortcuts(Scene &scene, const ResourceStore &resources)
{
    // Skipped while a text field or a slider owns the keyboard, or typing a
    // node name retargets the gizmo on every keystroke.
    if (ImGui::GetIO().WantCaptureKeyboard || ImGuizmo::IsUsing()) {
        return;
    }

    // 1/2/3 rather than the usual W/E/R: W and E are already the camera's
    // forward and up.
    if (ImGui::IsKeyPressed(ImGuiKey_1, false)) m_gizmoOperation = ImGuizmo::TRANSLATE;
    if (ImGui::IsKeyPressed(ImGuiKey_2, false)) m_gizmoOperation = ImGuizmo::ROTATE;
    if (ImGui::IsKeyPressed(ImGuiKey_3, false)) m_gizmoOperation = ImGuizmo::SCALE;

    if (ImGui::IsKeyPressed(ImGuiKey_X, false)) {
        m_gizmoMode = (m_gizmoMode == ImGuizmo::WORLD) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) &&
        m_selectionMode == SelectionMode::Node && m_selectedNode != 0) {
        deleteNode(m_selectedNode);
    }

    // F2 renames whatever is selected. The early return above means this never
    // fires while a rename field already has the keyboard.
    if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
        if (m_selectionMode == SelectionMode::Node && m_selectedNode != 0 &&
            scene.isAlive(m_selectedNode)) {
            beginRename(RenameTarget::Node, m_selectedNode, scene.getNode(m_selectedNode).name);
        } else if (m_selectionMode == SelectionMode::Material &&
                   m_selectedMaterial != 0 && m_selectedMaterial <= resources.materialCount()) {
            beginRename(RenameTarget::Material, m_selectedMaterial,
                        resources.material(m_selectedMaterial).name);
        }
    }
}

void EditorUI::drawGizmo(Scene &scene, const Camera &camera, uint32_t width, uint32_t height)
{
    m_gizmoHovered = false;

    if (m_selectionMode != SelectionMode::Node || m_selectedNode == 0 ||
        !scene.isAlive(m_selectedNode) || width == 0 || height == 0) {
        return;
    }

    // THE RECT IS THE WHOLE WINDOW, not the central dock node.
    //
    // The scene is drawn fullscreen into the swapchain and the dockspace is
    // laid over it with PassthruCentralNode -- the panels cover the render,
    // they do not shrink it. The camera's aspect ratio comes from the window
    // for the same reason. If the scene ever moves to an offscreen image
    // displayed in a viewport panel, this becomes that panel's rect and the
    // aspect ratio has to follow it.
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGuizmo::SetRect(viewport->Pos.x, viewport->Pos.y, viewport->Size.x, viewport->Size.y);
    ImGuizmo::SetOrthographic(false);

    const float aspect = static_cast<float>(width) / static_cast<float>(height);

    // The projection goes in WITHOUT a Y flip, matching what the shader gets.
    // The renderer flips Y with a negative-height viewport instead of baking
    // it into the matrix, so ImGuizmo's standard NDC-to-screen mapping lands
    // on the same pixels the rasteriser does. Bake proj[1][1] *= -1 into the
    // camera later and this needs the flip undone before it is handed over.
    const glm::mat4 view = camera.getViewMatrix();
    const glm::mat4 proj = camera.projection(aspect);

    // ImGuizmo manipulates a world matrix, and nodes store a local one.
    glm::mat4 world = scene.nodes().worldMatrix(m_selectedNode);

    const float snapValues[3] =
    {
        m_gizmoOperation == ImGuizmo::TRANSLATE ? m_snapTranslate
      : m_gizmoOperation == ImGuizmo::ROTATE    ? m_snapRotate
                                                : m_snapScale,
        0.0f, 0.0f
    };
    const float snapAll[3] = { snapValues[0], snapValues[0], snapValues[0] };

    const bool manipulated = ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(proj),
        static_cast<ImGuizmo::OPERATION>(m_gizmoOperation),
        static_cast<ImGuizmo::MODE>(m_gizmoMode),
        glm::value_ptr(world),
        nullptr,
        m_gizmoSnap ? snapAll : nullptr);

    // Queried before the early-out below: the picker has to know the cursor is
    // over an axis handle even on a frame where nothing was dragged, or the
    // click that grabs the gizmo also deselects the node.
    m_gizmoHovered = ImGuizmo::IsOver() || ImGuizmo::IsUsing();

    if (!manipulated) {
        return;
    }

    Node &node = scene.getNode(m_selectedNode);

    // world = parentWorld * local, so local = inverse(parentWorld) * world.
    // The parent's world matrix is this frame's: the transform pass ran in
    // drawItems() before the UI was built, and the parent has not moved since.
    const uint32_t parentId = node.parentId;
    const glm::mat4 local = parentId
        ? glm::inverse(scene.nodes().worldMatrix(parentId)) * world
        : world;

    node.setTransform(local);
    scene.invalidateDrawItems();

    // The inspector caches Euler angles per selected node and only refreshes
    // them when the selection changes. Rotating with the gizmo changes the
    // quaternion behind that cache's back, so force a reread.
    m_eulerOwner = 0;
}
