// ============================================================================
// EditorInteraction.cpp
// produces edits

// Nothing here mutates the scene directly. Everything goes on the command
// queue and is applied by Application after build() returns

#include "EditorUI.h"
#include "EditorCommands.h"
#include "../reflect/BinaryArchive.h"

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
    constexpr const char *kMaterialPayload = "EDITOR_MATERIAL";
    constexpr const char *kAssetPathPayload = "EDITOR_ASSET_PATH";
    constexpr const char *kTexturePayload = "EDITOR_TEXTURE";
}

void EditorUI::beginAssetDrag(const std::filesystem::path &path)
{
    if (!ImGui::BeginDragDropSource()) {
        return;
    }

    const std::string text = path.string();
    ImGui::SetDragDropPayload(kAssetPathPayload, text.c_str(), text.size() + 1);
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

void EditorUI::beginMaterialDrag(const ResourceStore &resources, uint32_t materialId)
{
    if (!ImGui::BeginDragDropSource()) {
        return;
    }

    ImGui::SetDragDropPayload(kMaterialPayload, &materialId, sizeof(uint32_t));

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
            // Copy , not alias: ImGui owns the payload buffer and reuses it
            // value outlives the frame on the command queue.
            materialId = *static_cast<const uint32_t *>(payload->Data);
        }
        ImGui::EndDragDropTarget();
    }
    return materialId;
}

void EditorUI::assignMaterial(Guid node, uint32_t subMesh, uint32_t materialId)
{
    if (node.isNull() || materialId == 0) {
        return;
    }
    EditorCommand cmd;
    cmd.kind       = EditorCommand::Kind::AssignMaterial;
    cmd.node       = node;
    cmd.subMesh    = subMesh;
    cmd.materialId = materialId;
    submit(std::move(cmd));
}

// ---------------------------------------------------------------------------
// create / delete
// ---------------------------------------------------------------------------

// Shared by the Hierarchy toolbar, the empty-space context menu and the
// per-node context menu. A null parent creates at the scene root.
void EditorUI::drawCreateMenuItems(Guid parent)
{
    if (ImGui::MenuItem("Empty Node")) {
        EditorCommand cmd;
        cmd.kind     = EditorCommand::Kind::CreateEmpty;
        cmd.parent   = parent;
        submit(std::move(cmd));
    }

    if (ImGui::MenuItem("Directional Light")) {
        EditorCommand cmd;
        cmd.kind     = EditorCommand::Kind::CreateLight;
        cmd.parent   = parent;
        submit(std::move(cmd));
    }

    ImGui::Separator();

    for (uint8_t i = 0; i < static_cast<uint8_t>(PrimitiveType::Count); ++i) {
        const auto type = static_cast<PrimitiveType>(i);
        if (ImGui::MenuItem(primitiveName(type))) {
            EditorCommand cmd;
            cmd.kind      = EditorCommand::Kind::CreatePrimitive;
            cmd.primitive = type;
            cmd.parent    = parent;
            submit(std::move(cmd));
        }
    }
}

void EditorUI::deleteNode(Guid node)
{
    if (node.isNull()) {
        return;
    }
    EditorCommand cmd;
    cmd.kind = EditorCommand::Kind::DeleteNode;
    cmd.node = node;
    submit(std::move(cmd));

}


// ---------------------------------------------------------------------------
// renaming
// ---------------------------------------------------------------------------

void EditorUI::beginRename(RenameTarget target, uint32_t id, const std::string &current)
{
    m_renameTarget = target;
    m_renameId     = id;
    m_renameNode   = {};
    m_renamePath.clear();
    std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", current.c_str());
    m_renameFocusPending = true;
}

void EditorUI::beginRenameNode(Guid node, const std::string &current)
{
    beginRename(RenameTarget::Node, 0, current);
    m_renameNode = node;
}

void EditorUI::beginRenameAsset(const std::filesystem::path &path)
{
    m_renameTarget = RenameTarget::Asset;
    m_renameId     = 0;
    m_renameNode   = {};
    m_renamePath   = path;
    std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s",
                  path.stem().string().c_str());
    m_renameFocusPending = true;
}

void EditorUI::cancelRename()
{
    m_renameTarget = RenameTarget::None;
    m_renameId     = 0;
    m_renameNode   = {};
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
        submit(std::move(cmd));
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void EditorUI::drawNodeContextMenu(Guid node, const std::string &name)
{
    if (!ImGui::BeginPopupContextItem()) {
        return;
    }

    if (ImGui::BeginMenu("Create Child")) {
        drawCreateMenuItems(node);
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Rename", "F2")) {
        beginRenameNode(node, name);
    }

    if (ImGui::MenuItem("Duplicate")) {
        EditorCommand cmd;
        cmd.kind = EditorCommand::Kind::DuplicateNode;
        cmd.node = node;
        submit(std::move(cmd));
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Delete", "Del")) {
        deleteNode(node);
    }

    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// gizmo
// ---------------------------------------------------------------------------

void EditorUI::drawGizmoToolbar()
{
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

        float *value = m_gizmoOperation == ImGuizmo::TRANSLATE ? &m_snapTranslate
                     : m_gizmoOperation == ImGuizmo::ROTATE    ? &m_snapRotate
                                                               : &m_snapScale;
        ImGui::DragFloat("##snap", value, 0.05f, 0.001f, 180.0f, "%.3f");
    }
}

void EditorUI::handleShortcuts(const Scene &scene, const ResourceStore &resources)
{
    if (ImGui::GetIO().WantCaptureKeyboard || ImGuizmo::IsUsing()) {
        return;
    }

    if (heldId() == 0) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
            submitSaveScene();
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) {
            submitLoadScene();
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
            submitUndo();
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) ||
            ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) {
            submitRedo();
        }
    }


    if (ImGui::IsKeyPressed(ImGuiKey_1, false)) m_gizmoOperation = ImGuizmo::TRANSLATE;
    if (ImGui::IsKeyPressed(ImGuiKey_2, false)) m_gizmoOperation = ImGuizmo::ROTATE;
    if (ImGui::IsKeyPressed(ImGuiKey_3, false)) m_gizmoOperation = ImGuizmo::SCALE;

    if (ImGui::IsKeyPressed(ImGuiKey_X, false)) {
        m_gizmoMode = (m_gizmoMode == ImGuizmo::WORLD) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) &&
        m_selectionMode == SelectionMode::Node && m_selectedSlot != 0) {
        deleteNode(m_selectedNode);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
        if (m_selectionMode == SelectionMode::Node && m_selectedSlot != 0) {
            beginRenameNode(m_selectedNode, scene.getNode(m_selectedSlot).name);
        } else if (m_selectionMode == SelectionMode::Material &&
                   m_selectedMaterial != 0 && m_selectedMaterial <= resources.materialCount()) {
            beginRename(RenameTarget::Material, m_selectedMaterial,
                        resources.material(m_selectedMaterial).name);
        }
    }
}

void EditorUI::drawGizmo(const Scene &scene, const Camera &camera, uint32_t width, uint32_t height)
{
    m_gizmoHovered = false;

    if (m_selectionMode != SelectionMode::Node || m_selectedSlot == 0 ||
        width == 0 || height == 0) {
        return;
    }

    // THE RECT IS THE WHOLE WINDOW, not the central dock node.
    //
    // The scene is drawn fullscreen into the swapchain and the dockspace is
    // laid over it with PassthruCentralNode
    // The camera's aspect ratio comes from the window for the same reason.
    // If the scene ever moves to an offscreen image
    // displayed in a viewport panel, this becomes that panel's rect and the
    // aspect ratio has to follow it.

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGuizmo::SetRect(viewport->Pos.x, viewport->Pos.y, viewport->Size.x, viewport->Size.y);
    ImGuizmo::SetOrthographic(false);

    const float aspect = static_cast<float>(width) / static_cast<float>(height);

    const glm::mat4 view = camera.getViewMatrix();
    const glm::mat4 proj = camera.projection(aspect);

    if (!ImGuizmo::IsUsing() || m_gizmoNode != m_selectedNode) {
        m_gizmoWorld = scene.nodes().worldMatrix(m_selectedSlot);
        m_gizmoNode  = m_selectedNode;
    }
    glm::mat4 world = m_gizmoWorld;

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

    m_gizmoHovered = ImGuizmo::IsOver() || ImGuizmo::IsUsing();

    if (!manipulated) {
        return;
    }
    m_gizmoWorld = world;

    const Node &node = scene.getNode(m_selectedSlot);

    // world = parentWorld * local, so local = inverse(parentWorld) * world.
    const uint32_t parentId = node.parentId;
    const glm::mat4 local = parentId
        ? glm::inverse(scene.nodes().worldMatrix(parentId)) * world
        : world;

    Node edited = node;
    edited.setTransform(local);

    const char *name = m_gizmoOperation == ImGuizmo::TRANSLATE ? "Move"
                     : m_gizmoOperation == ImGuizmo::ROTATE    ? "Rotate"
                                                               : "Scale";
    submitModify(EditTarget::forNode(m_selectedNode), reflect::toBlob(edited), name);
}
