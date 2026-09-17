#ifndef ASSET_DIR
#define ASSET_DIR "./"
#endif

#include "EditorUI.h"

#include <volk.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
#include <SDL3/SDL.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <string>

#include "../render/core/VulkanContext.h"
#include "../render/resources/GeometryStore.h"
#include "../render/resources/ResourceStore.h"
#include "../render/passes/ShadowMap.h"      // ShadowSettings
#include "../common/constants.h"
#include "../scene/Scene.h"
#include "../reflect/BinaryArchive.h"
#include "UndoHistory.h"
#include "../scene/Camera.h"
#include "../common/errors.h"
#include <ImGuizmo.h>
#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <cctype>
#include <functional>
#include <vector>
#include <imgui_internal.h>     // for PushMultiItemsWidths
#include <glm/vec3.hpp>

void EditorUI::applyTheme()
{
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();

    // ---------------------------------------------------------------------
    // Shape
    // ---------------------------------------------------------------------

    style.Alpha = 1.0f;

    style.WindowPadding     = ImVec2(8.0f, 8.0f);
    style.FramePadding      = ImVec2(6.0f, 4.0f);
    style.CellPadding       = ImVec2(4.0f, 3.0f);
    style.ItemSpacing       = ImVec2(6.0f, 5.0f);
    style.ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
    style.IndentSpacing     = 16.0f;

    style.ScrollbarSize     = 12.0f;
    style.GrabMinSize       = 10.0f;

    style.WindowRounding    = 3.0f;
    style.ChildRounding     = 2.0f;
    style.FrameRounding     = 2.0f;
    style.PopupRounding     = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding      = 2.0f;
    style.TabRounding       = 2.0f;

    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;

    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_None;

    // -------------- Palette --------------

    const ImVec4 bg          = ImVec4(0.060f, 0.060f, 0.065f, 1.0f);
    const ImVec4 bgDark      = ImVec4(0.045f, 0.045f, 0.050f, 1.0f);
    const ImVec4 panel       = ImVec4(0.075f, 0.075f, 0.080f, 1.0f);

    const ImVec4 control     = ImVec4(0.110f, 0.110f, 0.115f, 1.0f);
    const ImVec4 hover       = ImVec4(0.145f, 0.145f, 0.250f, 1.0f);
    const ImVec4 active      = ImVec4(0.175f, 0.175f, 0.180f, 1.0f);

    const ImVec4 border      = ImVec4(0.190f, 0.190f, 0.200f, 1.0f);
    const ImVec4 borderLight = ImVec4(0.240f, 0.240f, 0.250f, 1.0f);

    const ImVec4 text        = ImVec4(0.860f, 0.860f, 0.870f, 1.0f);
    const ImVec4 textDim     = ImVec4(0.500f, 0.500f, 0.510f, 1.0f);

    // Blue is intentionally subtle.
    const ImVec4 blue        = ImVec4(0.260f, 0.590f, 0.980f, 1.0f);
    const ImVec4 blueSoft    = ImVec4(0.260f, 0.590f, 0.980f, 0.35f);
    const ImVec4 blueDim     = ImVec4(0.260f, 0.590f, 0.980f, 0.18f);

    ImVec4* c = style.Colors;

    c[ImGuiCol_Text]         = text;
    c[ImGuiCol_TextDisabled] = textDim;


    // Windows / panels

    c[ImGuiCol_WindowBg]     = bg;
    c[ImGuiCol_ChildBg]      = bgDark;
    c[ImGuiCol_PopupBg]      = panel;

    c[ImGuiCol_Border]       = border;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

    // Inputs / controls

    c[ImGuiCol_FrameBg]        = control;
    c[ImGuiCol_FrameBgHovered] = hover;
    c[ImGuiCol_FrameBgActive]  = active;

    // ---------------------------------------------------------------------
    // Title bars

    c[ImGuiCol_TitleBg]          = bgDark;
    c[ImGuiCol_TitleBgActive]    = panel;
    c[ImGuiCol_TitleBgCollapsed] = bgDark;

    c[ImGuiCol_MenuBarBg] = bgDark;

    // Scrollbars

    c[ImGuiCol_ScrollbarBg]     = bgDark;
    c[ImGuiCol_ScrollbarGrab]   = control;
    c[ImGuiCol_ScrollbarGrabHovered] = hover;
    c[ImGuiCol_ScrollbarGrabActive]  = active;

    // ---------------------------------------------------------------------
    // Buttons

    c[ImGuiCol_Button]        = control;
    c[ImGuiCol_ButtonHovered] = hover;
    c[ImGuiCol_ButtonActive]  = active;

    // ---------------------------------------------------------------------
    // Headers / tree nodes

    c[ImGuiCol_Header]        = control;
    c[ImGuiCol_HeaderHovered] = hover;
    c[ImGuiCol_HeaderActive]  = active;

    // ---------------------------------------------------------------------
    // Checkboxes / sliders


    c[ImGuiCol_CheckMark]      = blue;
    c[ImGuiCol_SliderGrab]     = blueSoft;
    c[ImGuiCol_SliderGrabActive] = blue;

    // ---------------------------------------------------------------------
    // Separators


    c[ImGuiCol_Separator]        = border;
    c[ImGuiCol_SeparatorHovered] = borderLight;
    c[ImGuiCol_SeparatorActive]  = blueSoft;

    // ---------------------------------------------------------------------
    // Resize grips

    c[ImGuiCol_ResizeGrip]        = control;
    c[ImGuiCol_ResizeGripHovered] = hover;
    c[ImGuiCol_ResizeGripActive]  = blue;

    // ---------------------------------------------------------------------
    // Tabs

    c[ImGuiCol_Tab]                = bgDark;
    c[ImGuiCol_TabHovered]         = hover;
    c[ImGuiCol_TabSelected]        = panel;
    c[ImGuiCol_TabSelectedOverline] = blue;
    c[ImGuiCol_TabDimmed]          = bgDark;
    c[ImGuiCol_TabDimmedSelected]  = panel;
    c[ImGuiCol_TabDimmedSelectedOverline] = blueDim;

    // ---------------------------------------------------------------------
    // Tables

    c[ImGuiCol_TableHeaderBg]     = control;
    c[ImGuiCol_TableBorderStrong] = border;
    c[ImGuiCol_TableBorderLight]  = ImVec4(0.130f, 0.130f, 0.135f, 1.0f);

    c[ImGuiCol_TableRowBg]        = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]     = ImVec4(0.080f, 0.080f, 0.085f, 1.0f);

    // ---------------------------------------------------------------------
    // Selection / navigation


    c[ImGuiCol_TextSelectedBg] = blueDim;
    c[ImGuiCol_NavCursor]      = blue;

    // ---------------------------------------------------------------------
    // Drag & drop

    c[ImGuiCol_DragDropTarget] = blue;

    // ---------------------------------------------------------------------
    // Modal dimming

    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
}

// ---------------------------------------------------------------------------
// property rows
// ---------------------------------------------------------------------------

// A two-column table: labels left, controls right, all aligned. Without this
// every row's control starts at a different x and the panel looks ragged.
void EditorUI::beginProperties(const char *id)
{
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 4.0f));
    ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp);
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 76.0f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
}

void EditorUI::endProperties()
{
    ImGui::EndTable();
    ImGui::PopStyleVar();
}


bool EditorUI::vec3Control(const char *label, glm::vec3 &values,
                           float resetValue, float speed)
{
    bool changed = false;

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);

    ImGui::TableSetColumnIndex(1);
    ImGui::PushID(label);

    const float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
    const ImVec2 buttonSize(lineHeight*.2f, lineHeight);

    // Divides the remaining width into three equal fields, accounting for buttons
    ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth() - buttonSize.x * 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

    struct Axis { const char *name; ImVec4 base, hover, active; float *value; };
    const Axis axes[3]
    {
        { "X", ImVec4(0.72f, 0.14f, 0.18f, 1.0f), ImVec4(0.82f, 0.32f, 0.36f, 1.0f),
               ImVec4(0.72f, 0.24f, 0.28f, 1.0f), &values.x },
        { "Y", ImVec4(0.35f, 0.62f, 0.28f, 1.0f), ImVec4(0.43f, 0.72f, 0.36f, 1.0f),
               ImVec4(0.35f, 0.62f, 0.28f, 1.0f), &values.y },
        { "Z", ImVec4(0.24f, 0.44f, 0.76f, 1.0f), ImVec4(0.32f, 0.52f, 0.86f, 1.0f),
               ImVec4(0.24f, 0.44f, 0.76f, 1.0f), &values.z }
    };

    for (int i = 0; i < 3; ++i) {
        const Axis &axis = axes[i];

        ImGui::PushStyleColor(ImGuiCol_Button,        axis.base);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, axis.hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  axis.active);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

        ImGui::PushID(i);
        if (ImGui::Button(axis.name, buttonSize)) {
            *axis.value = resetValue;
            changed = true;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        changed |= ImGui::DragFloat("##v", axis.value, speed, 0.0f, 0.0f, "%.3f");
        ImGui::PopID();
        ImGui::PopItemWidth();

        if (i < 2) {
            ImGui::SameLine();
        }
    }

    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

// ---------------------------------------------------------------------------
// panels


namespace
{
    bool sliderRow(const char *label, float &value, float min, float max,
                   const char *format = "%.3f")
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);

        ImGui::TableSetColumnIndex(1);
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool changed = ImGui::SliderFloat("##v", &value, min, max, format);
        ImGui::PopID();
        return changed;
    }
}

void EditorUI::drawShadowWindow()
{
    if (!m_showShadowWindow) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Shadows", &m_showShadowWindow)) {
        if (!m_shadowSettings) {
            ImGui::TextUnformatted("No renderer bound.");
        } else {
            ShadowSettings &s = *m_shadowSettings;

            ImGui::Checkbox("Enabled", &s.enabled);
            ImGui::Separator();

            beginProperties("shadow_fit");
            sliderRow("Distance", s.distance, 2.0f, 200.0f, "%.1f");
            if (m_sunDirection) {
                // Renormalised by the renderer every frame, so dragging a
                // component to zero is safe.
                vec3Control("Sun dir", *m_sunDirection, 0.0f, 0.01f);
            }
            endProperties();

            ImGui::SeparatorText("Bias");
            beginProperties("shadow_bias");
            sliderRow("Normal",   s.normalBias,   0.0f, 4.0f, "%.2f texels");
            sliderRow("Depth",    s.depthBias,    0.0f, 0.01f, "%.5f");
            sliderRow("Constant", s.constantBias, 0.0f, 8.0f, "%.2f");
            sliderRow("Slope",    s.slopeBias,    0.0f, 8.0f, "%.2f");
            endProperties();

            ImGui::Spacing();
            ImGui::TextDisabled("Raise Slope first if acne appears.");
            ImGui::TextDisabled("Sun dir is the fallback -- a Directional\n"
                                "Light node overrides it.");
        }
    }
    ImGui::End();
}

bool EditorUI::drawLightSection(Node &node)
{
    if (node.lightType != LightType::Directional) {
        return false;
    }

    if (!ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
        return false;
    }

    bool changed = false;

    beginProperties("light_props");

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Color");
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    changed |= ImGui::ColorEdit3("##lightcolor", &node.lightColor.x);

    changed |= sliderRow("Intensity", node.lightIntensity, 0.0f, 20.0f, "%.2f");

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Shadows");
    ImGui::TableSetColumnIndex(1);
    changed |= ImGui::Checkbox("##lightshadows", &node.lightCastsShadows);

    endProperties();

    // Read-only: aiming happens through the rotation above or the gizmo, and a
    // second way to set the same thing would fight it.
    const glm::vec3 direction = glm::normalize(node.getRotation() * glm::vec3(0.0f, 0.0f, -1.0f));
    ImGui::TextDisabled("Direction  %.2f, %.2f, %.2f", direction.x, direction.y, direction.z);
    ImGui::TextDisabled("Rotate the node to aim it.");
    return changed;
}

void EditorUI::build(const Scene &scene, const GeometryStore &geometry, const ResourceStore &resources,
                     const Camera &camera, uint32_t width, uint32_t height)
{
    resolveSelection(scene);

    // Submitted before anything reads WorkPos: the main menu bar is what
    // shrinks the viewport's work area, and the dockspace below sizes itself
    // from it.
    if (ImGui::BeginMainMenuBar()) {
        drawEditMenu();
        if (ImGui::BeginMenu("Window")) {
            ImGui::MenuItem("Shadows", nullptr, &m_showShadowWindow);
            ImGui::EndMenu();
        }

        drawSceneStatus();
        ImGui::EndMainMenuBar();
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGuiWindowFlags dockspaceFlags = ImGuiWindowFlags_NoTitleBar |
                                      ImGuiWindowFlags_NoCollapse |
                                      ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoBringToFrontOnFocus |
                                      ImGuiWindowFlags_NoNavFocus |
                                      ImGuiWindowFlags_NoBackground;

    ImGui::Begin("EditorDockSpaceWindow", nullptr, dockspaceFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");

    // If there is no layout loaded from INI, build the default layout
    if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID dockMain = dockspaceId;
        ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.30f, nullptr, &dockMain);
        ImGuiID dockLeft   = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.20f, nullptr, &dockMain);
        ImGuiID dockRight  = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.25f, nullptr, &dockMain);

        ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
        ImGui::DockBuilderDockWindow("Inspector", dockRight);
        ImGui::DockBuilderDockWindow("Project", dockBottom);
        ImGui::DockBuilderFinish(dockspaceId);
    }

    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    // Draw Panels
    if (ImGui::Begin("Hierarchy", nullptr, ImGuiWindowFlags_NoCollapse)) {
        drawHierarchy(scene, geometry);
    }
    ImGui::End();

    if (ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse)) {
        drawGizmoToolbar();
        ImGui::Separator();
        drawInspector(scene, geometry, resources);
    }
    ImGui::End();

    // Requires MenuBar flag for our Tabs
    if (ImGui::Begin("Project", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar)) {
        drawProjectPanel(resources);
    }
    ImGui::End();

    ImGui::End(); // End EditorDockSpaceWindow

    drawShadowWindow();

    handleShortcuts(scene, resources);
    drawSaveMaterialPopup(resources);
    drawScenePopups();
    drawGizmo(scene, camera, width, height);

    m_recorder.endFrame(m_commands, heldId());
}

void EditorUI::drawEditMenu()
{
    if (ImGui::BeginMenu("File")) {          // was: if (!ImGui::BeginMenu ...
        if (ImGui::MenuItem("New Scene", "Ctrl+N"))          { requestSceneAction(SceneAction::New); }
        if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))      { openScenePicker(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Save Scene", "Ctrl+S"))         { saveScene(false); }
        if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) { saveScene(true); }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        const bool canUndo = m_history && m_history->canUndo();
        const bool canRedo = m_history && m_history->canRedo();

        // "###" keeps the item ID stable while the label names the step.
        const std::string undoLabel = canUndo ? "Undo " + m_history->undoName() + "###undo" : "Undo###undo";
        const std::string redoLabel = canRedo ? "Redo " + m_history->redoName() + "###redo" : "Redo###redo";

        if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, canUndo)) {
            submitUndo();
        }
        if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, canRedo)) {
            submitRedo();
        }
        ImGui::EndMenu();
    }
}

void EditorUI::submitUndo()
{
    EditorCommand cmd;
    cmd.kind = EditorCommand::Kind::Undo;
    submit(std::move(cmd));
}

void EditorUI::submitRedo()
{
    EditorCommand cmd;
    cmd.kind = EditorCommand::Kind::Redo;
    submit(std::move(cmd));
}

EditorSelection EditorUI::selection() const
{
    EditorSelection out;
    switch (m_selectionMode) {
    case SelectionMode::None:     out.mode = EditorSelection::Mode::None;     break;
    case SelectionMode::Node:     out.mode = EditorSelection::Mode::Node;     break;
    case SelectionMode::Material: out.mode = EditorSelection::Mode::Material; break;
    case SelectionMode::Texture:  out.mode = EditorSelection::Mode::Texture;  break;
    }
    out.node     = m_selectedNode;
    out.subMesh  = static_cast<uint32_t>(m_selectedSubMesh);
    out.material = m_selectedMaterial;
    out.texture  = m_selectedTexture;
    return out;
}

void EditorUI::setSelection(const EditorSelection &selection)
{
    switch (selection.mode) {
    case EditorSelection::Mode::None:
        clearSelection();
        break;
    case EditorSelection::Mode::Node:
        selectNode(selection.node, selection.subMesh);
        break;
    case EditorSelection::Mode::Material:
        selectMaterial(selection.material);
        break;
    case EditorSelection::Mode::Texture:
        clearSelection();
        m_selectionMode   = SelectionMode::Texture;
        m_selectedTexture = selection.texture;
        break;
    }
}

void EditorUI::submit(EditorCommand command)
{
    m_recorder.push(m_commands, std::move(command));
}

void EditorUI::submitModify(const EditTarget &target, reflect::Blob snapshot, const char *name)
{
    m_recorder.modify(m_commands, target, std::move(snapshot), name, heldId());
}

EditRecorder::HeldId EditorUI::heldId() const
{
    // Above the 32-bit ImGuiID range, so it can never equal a widget ID.
    static constexpr EditRecorder::HeldId GizmoHeld = EditRecorder::HeldId{ 1 } << 32;
    if (ImGuizmo::IsUsing()) {
        return GizmoHeld;
    }
    return ImGui::GetActiveID();
}

void EditorUI::resolveSelection(const Scene &scene)
{
    m_selectedSlot = 0;
    if (m_selectionMode != SelectionMode::Node) {
        return;
    }
    m_selectedSlot = scene.findNode(m_selectedNode);
    if (m_selectedSlot == 0) {
        clearSelection();
    }
}

namespace
{
    // Case-insensitive substring test. ASCII-only

    bool matchesFilter(std::string_view haystack, std::string_view needle)
    {
        if (needle.empty()) {
            return true;
        }
        if (needle.size() > haystack.size()) {
            return false;
        }
        const auto lower = [](char c) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        };
        for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
            size_t j = 0;
            while (j < needle.size() && lower(haystack[i + j]) == lower(needle[j])) {
                ++j;
            }
            if (j == needle.size()) {
                return true;
            }
        }
        return false;
    }
}

bool EditorUI::searchBar(const char *id, std::string &filter)
{
    ImGui::PushID(id);
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%s", filter.c_str());

    const float clearWidth = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(-(clearWidth + ImGui::GetStyle().ItemSpacing.x));
    if (ImGui::InputTextWithHint("##search", "Search...", buffer, sizeof(buffer))) {
        filter = buffer;
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(filter.empty());
    if (ImGui::Button("x", ImVec2(clearWidth, 0.0f))) {
        filter.clear();
    }
    ImGui::EndDisabled();

    ImGui::PopID();
    return !filter.empty();
}

namespace
{
    struct ProjectItem
    {
        std::string label;
        ImTextureID thumbnail = 0;                      // 0 -> use colour
        ImVec4      color{ 0.3f, 0.3f, 0.32f, 1.0f };
        const char *badge = nullptr;                    // "DIR", "GLTF", "MAT"
        uint32_t    payload = 0;                        // material id, or index into the entry list
        bool        renaming = false;                   // draw the label as an edit field
    };

    // Below this val : list view takes over.
    constexpr float ListThreshold = 26.0f;

    struct ProjectItemActions
    {
        std::function<bool(const ProjectItem &)> isSelected;
        std::function<void(const ProjectItem &)> onClick;
        std::function<void(const ProjectItem &)> onActivate;     // double click
        std::function<void(const ProjectItem &)> onDragSource;   // called right after the widget
        std::function<void(const ProjectItem &)> onContextMenu;  // right-click on the item itself

        std::function<bool(const ProjectItem &)> drawRename;
    };

    void drawProjectItemsGrid(const std::vector<ProjectItem> &items, float thumbSize,
                              const ProjectItemActions &actions)
    {
        const float cellSize = thumbSize + 16.0f;
        const float width    = ImGui::GetContentRegionAvail().x;
        const int   columns  = std::max(1, static_cast<int>(width / cellSize));

        if (!ImGui::BeginTable("ProjectGrid", columns)) {
            return;
        }

        for (const ProjectItem &item : items) {
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(item.payload));

            const bool   selected = actions.isSelected && actions.isSelected(item);
            const ImVec2 origin   = ImGui::GetCursorScreenPos();

            if (selected) {
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(origin.x - 4.0f, origin.y - 4.0f),
                    ImVec2(origin.x + thumbSize + 4.0f,
                           origin.y + thumbSize + ImGui::GetTextLineHeight() * 2.0f),
                    ImGui::GetColorU32(ImGuiCol_ButtonActive), 4.0f);
            }

            bool clicked = false;
            if (item.thumbnail) {
                clicked = ImGui::ImageButton("##thumb", item.thumbnail,
                                             ImVec2(thumbSize, thumbSize));
            } else {
                clicked = ImGui::ColorButton("##thumb", item.color,
                                             ImGuiColorEditFlags_NoTooltip |
                                             ImGuiColorEditFlags_NoDragDrop,
                                             ImVec2(thumbSize, thumbSize));
            }

            if (actions.onDragSource) {
                actions.onDragSource(item);
            }

            const bool activated = ImGui::IsItemHovered() &&
                                   ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

            if (actions.onContextMenu) {
                actions.onContextMenu(item);
            }

            if (item.badge && !item.thumbnail) {
                const ImVec2 textSize = ImGui::CalcTextSize(item.badge);
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(origin.x + (thumbSize - textSize.x) * 0.5f,
                           origin.y + (thumbSize - textSize.y) * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_Text), item.badge);
            }

            if (item.renaming && actions.drawRename) {
                ImGui::PushItemWidth(thumbSize);
                actions.drawRename(item);
                ImGui::PopItemWidth();
            } else {
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbSize);
                ImGui::TextUnformatted(item.label.c_str());
                ImGui::PopTextWrapPos();
            }

            if (activated && actions.onActivate) {
                actions.onActivate(item);
            } else if (clicked && actions.onClick) {
                actions.onClick(item);
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    void drawProjectItemsList(const std::vector<ProjectItem> &items,
                              const ProjectItemActions &actions)
    {
        const float rowHeight = ImGui::GetTextLineHeight();

        for (const ProjectItem &item : items) {
            ImGui::PushID(static_cast<int>(item.payload));

            if (item.thumbnail) {
                ImGui::Image(item.thumbnail, ImVec2(rowHeight, rowHeight));
            } else {
                ImGui::ColorButton("##swatch", item.color,
                                   ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                                   ImVec2(rowHeight, rowHeight));
            }
            ImGui::SameLine();

            if (item.renaming && actions.drawRename) {
                actions.drawRename(item);
                ImGui::PopID();
                continue;
            }

            std::string row;
            if (item.badge) {
                row += "[";
                row += item.badge;
                row += "] ";
            }
            row += item.label;

            const bool selected = actions.isSelected && actions.isSelected(item);
            const bool clicked  = ImGui::Selectable(row.c_str(), selected,
                                                    ImGuiSelectableFlags_AllowDoubleClick);

            if (actions.onDragSource) {
                actions.onDragSource(item);
            }
            if (actions.onContextMenu) {
                actions.onContextMenu(item);
            }

            if (clicked) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (actions.onActivate) {
                        actions.onActivate(item);
                    }
                } else if (actions.onClick) {
                    actions.onClick(item);
                }
            }
            ImGui::PopID();
        }
    }

    // Lowercased extension
    std::string lowerExtension(const std::filesystem::path &path)
    {
        std::string ext = path.extension().string();
        for (char &c : ext) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return ext;
    }

    bool isImageExtension(const std::string &ext)
    {
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
               ext == ".tga" || ext == ".bmp";
    }
}

void EditorUI::drawProjectPanel(const ResourceStore &resources)
{
    if (ImGui::BeginMenuBar()) {
        if (ImGui::MenuItem("Assets", nullptr, m_projectTab == ProjectTab::Assets)) {
            m_projectTab = ProjectTab::Assets;
        }
        if (ImGui::MenuItem("Materials", nullptr, m_projectTab == ProjectTab::Materials)) {
            m_projectTab = ProjectTab::Materials;
        }
        if (ImGui::MenuItem("Textures", nullptr, m_projectTab == ProjectTab::Textures)) {
            m_projectTab = ProjectTab::Textures;
        }

        if (m_projectTab != ProjectTab::Assets) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);

            const char *labels[] = { "All", "Project", "Imported" };
            int current = static_cast<int>(m_originFilter);
            if (ImGui::Combo("##origin", &current, labels, IM_ARRAYSIZE(labels))) {
                m_originFilter = static_cast<OriginFilter>(current);
            }
        }
        constexpr float sliderWidth = 110.0f;
        const float     sliderX     = ImGui::GetContentRegionMax().x - sliderWidth;
        if (sliderX > ImGui::GetCursorPosX()) {
            ImGui::SameLine(sliderX);
            ImGui::SetNextItemWidth(sliderWidth);
            ImGui::SliderFloat("##zoom", &m_thumbnailSize, 16.0f, 128.0f, "");
            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted("Thumbnail size -- drag fully left for list view");
                ImGui::EndTooltip();
            }
        }
        m_projectView = m_thumbnailSize <= ListThreshold ? ProjectView::List : ProjectView::Grid;

        ImGui::EndMenuBar();
    }

    switch (m_projectTab) {
    case ProjectTab::Assets:    drawAssetsTab();            break;
    case ProjectTab::Materials: drawMaterialsTab(resources); break;
    case ProjectTab::Textures:  drawTexturesTab(resources);  break;
    }
}

bool EditorUI::passesOriginFilter(AssetOrigin origin) const
{
    switch (m_originFilter) {
    case OriginFilter::Project:  return origin == AssetOrigin::Project;
    case OriginFilter::Imported: return origin == AssetOrigin::Imported;
    case OriginFilter::All:
    default:                     return true;
    }
}

void EditorUI::drawAssetsTab()
{
    ImGui::TextDisabled("Current Path:");
    ImGui::SameLine();
    ImGui::TextUnformatted(m_currentAssetPath.string().c_str());

    const std::filesystem::path rootPath   = std::filesystem::absolute(ASSET_DIR);
    const std::filesystem::path currentAbs = std::filesystem::absolute(m_currentAssetPath);

    if (currentAbs != rootPath) {
        if (ImGui::Button("<- Up")) {
            m_currentAssetPath = m_currentAssetPath.parent_path();
            m_selectedAsset.clear();
        }
        ImGui::Separator();
    }

    const bool filtering = searchBar("assetsearch", m_assetFilter);
    ImGui::Separator();

    // Built before anything is drawn: the grid needs the count to lay out
    // columns, and payload indexes m_assetEntries.
    m_assetEntries.clear();
    std::vector<ProjectItem> items;

    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(m_currentAssetPath, ec)) {
        const std::string filename = entry.path().filename().string();
        if (!matchesFilter(filename, m_assetFilter)) {
            continue;
        }

        ProjectItem item;
        item.label    = filename;
        item.payload  = static_cast<uint32_t>(m_assetEntries.size());
        item.renaming = isRenamingAsset(entry.path());

        if (entry.is_directory()) {
            item.badge = "DIR";
            item.color = ImVec4(0.32f, 0.30f, 0.22f, 1.0f);
        } else {
            const std::string ext = lowerExtension(entry.path());
            if (ext == ".gltf" || ext == ".glb") {
                item.badge = "GLTF";
                item.color = ImVec4(0.20f, 0.28f, 0.36f, 1.0f);
            } else if (ext == ".mat") {
                item.badge = "MAT";
                item.color = ImVec4(0.30f, 0.22f, 0.34f, 1.0f);
            } else if (ext == ".scene") {
                item.badge = "SCENE";
                item.color = ImVec4(0.36f, 0.26f, 0.18f, 1.0f);
            } else if (isImageExtension(ext)) {
                // No thumbnail: a file on disk is not a loaded texture, and
                // decoding every image in a folder just to browse it would
                // upload a lot of VRAM nobody asked for.
                item.badge = "IMG";
                item.color = ImVec4(0.22f, 0.30f, 0.24f, 1.0f);
            } else {
                item.badge = "FILE";
            }
        }

        m_assetEntries.push_back(entry.path());
        items.push_back(std::move(item));
    }

    ProjectItemActions actions;
    actions.isSelected = [this](const ProjectItem &item)
    {
        return !m_selectedAsset.empty() && m_assetEntries[item.payload] == m_selectedAsset;
    };
    actions.onClick = [this](const ProjectItem &item)
    {
        m_selectedAsset = m_assetEntries[item.payload];
    };
    actions.onActivate = [this](const ProjectItem &item)
    {
        const std::filesystem::path &path = m_assetEntries[item.payload];
        m_selectedAsset = path;

        std::error_code dirEc;
        if (std::filesystem::is_directory(path, dirEc)) {
            m_currentAssetPath = path;
            m_selectedAsset.clear();
            return;
        }

        const std::string ext = lowerExtension(path);
        if (ext == ".gltf" || ext == ".glb") {
            EditorCommand cmd;
            cmd.kind = EditorCommand::Kind::LoadModel;
            cmd.path = path;
            submit(std::move(cmd));
        } else if (ext == ".mat") {
            EditorCommand cmd;
            cmd.kind = EditorCommand::Kind::LoadMaterial;
            cmd.path = path;
            submit(std::move(cmd));
        } else if (ext == ".scene") {
            requestSceneAction(SceneAction::Open, path);
        }
    };
    actions.onDragSource = [this](const ProjectItem &item)
    {
        const std::filesystem::path &path = m_assetEntries[item.payload];
        if (isImageExtension(lowerExtension(path))) {
            beginAssetDrag(path);
        }
    };
    actions.onContextMenu = [this](const ProjectItem &item)
    {
        if (!ImGui::BeginPopupContextItem()) {
            return;
        }
        if (ImGui::MenuItem("Rename", "F2")) {
            beginRenameAsset(m_assetEntries[item.payload]);
        }
        ImGui::EndPopup();
    };
    actions.drawRename = [this](const ProjectItem &item)
    {
        if (!renameField("##assetrename")) {
            return false;
        }
        EditorCommand cmd;
        cmd.kind = EditorCommand::Kind::RenameAsset;
        cmd.path = m_assetEntries[item.payload];
        cmd.name = m_renameBuffer;
        submit(std::move(cmd));
        cancelRename();
        return true;
    };

    ImGui::BeginChild("AssetList");
    if (m_projectView == ProjectView::Grid) {
        drawProjectItemsGrid(items, m_thumbnailSize, actions);
    } else {
        drawProjectItemsList(items, actions);
    }

    if (items.empty()) {
        ImGui::TextDisabled(filtering ? "No matches" : "Empty folder");
    }

    drawAssetContextMenu();
    ImGui::EndChild();
}

void EditorUI::drawAssetContextMenu()
{
    // NoOpenOverItems so right-clicking a file does not get the create menu
    if (!ImGui::BeginPopupContextWindow("assetcontext",
                                        ImGuiPopupFlags_MouseButtonRight |
                                        ImGuiPopupFlags_NoOpenOverItems)) {
        return;
    }

    if (ImGui::MenuItem("New Folder")) {
        EditorCommand cmd;
        cmd.kind = EditorCommand::Kind::CreateDirectory;
        cmd.path = m_currentAssetPath / "New Folder";
        submit(std::move(cmd));
    }

    if (ImGui::MenuItem("New Material")) {
        EditorCommand cmd;
        cmd.kind = EditorCommand::Kind::CreateMaterial;
        cmd.path = m_currentAssetPath / "New Material.mat";
        submit(std::move(cmd));
    }

    ImGui::EndPopup();
}

void EditorUI::drawMaterialsContextMenu()
{
    if (!ImGui::BeginPopupContextWindow("materialcontext",
                                        ImGuiPopupFlags_MouseButtonRight |
                                        ImGuiPopupFlags_NoOpenOverItems)) {
        return;
    }

    if (ImGui::MenuItem("New Material")) {
        EditorCommand cmd;
        cmd.kind = EditorCommand::Kind::CreateMaterial;
        submit(std::move(cmd));
    }

    ImGui::EndPopup();
}

void EditorUI::drawMaterialsTab(const ResourceStore &resources)
{
    const bool filtering = searchBar("matsearch", m_materialFilter);
    ImGui::Separator();

    std::vector<ProjectItem> items;
    for (uint32_t i = 1; i <= resources.materialCount(); ++i) {
        const Material &mat = resources.material(i);

        if (!passesOriginFilter(resources.materialInfo(i).origin)) {
            continue;
        }

        const std::string name = mat.name.empty() ? "Material " + std::to_string(i) : mat.name;
        if (!matchesFilter(name, m_materialFilter)) {
            continue;
        }

        ProjectItem item;
        item.payload  = i;
        item.renaming = isRenaming(RenameTarget::Material, i);
        item.label    = resources.materialInfo(i).dirty ? name + " *" : name;
        item.color   = ImVec4(mat.baseColorFactor.r, mat.baseColorFactor.g,
                              mat.baseColorFactor.b, 1.0f);

        if (mat.baseColorTexture != 0 && mat.baseColorTexture <= resources.textureCount()) {
            if (const VkDescriptorSet set = texturePreview(resources, mat.baseColorTexture)) {
                item.thumbnail = reinterpret_cast<ImTextureID>(set);
            }
        }
        items.push_back(std::move(item));
    }

    ProjectItemActions actions;
    actions.isSelected = [this](const ProjectItem &item)
    {
        return m_selectionMode == SelectionMode::Material && m_selectedMaterial == item.payload;
    };
    actions.onClick = [this](const ProjectItem &item)
    {
        selectMaterial(item.payload);
    };
    actions.onActivate = actions.onClick;
    actions.onDragSource = [this, &resources](const ProjectItem &item)
    {
        beginMaterialDrag(resources, item.payload);
    };
    actions.onContextMenu = [this, &resources](const ProjectItem &item)
    {
        if (!ImGui::BeginPopupContextItem()) {
            return;
        }
        if (ImGui::MenuItem("Rename", "F2")) {
            beginRename(RenameTarget::Material, item.payload,
                        resources.material(item.payload).name);
        }
        if (ImGui::MenuItem("Save As...")) {
            m_saveAsRequested = true;
            m_saveAsMaterial  = item.payload;
            std::snprintf(m_saveAsBuffer, sizeof(m_saveAsBuffer), "%s",
                          resources.material(item.payload).name.c_str());
        }
        ImGui::EndPopup();
    };
    actions.drawRename = [this, &resources](const ProjectItem &item)
    {
        if (!renameField("##matrename")) {
            return false;
        }
        // A snapshot of the renamed copy
        // Applying it goes through updateMaterial, which marks the material
        // dirty -> asterisk appears
        Material renamed = resources.material(item.payload);
        renamed.name = m_renameBuffer;
        submitModify(EditTarget::forMaterial(item.payload), reflect::toBlob(renamed),
                     "Rename Material");
        cancelRename();
        return true;
    };

    ImGui::BeginChild("MaterialList");
    if (m_projectView == ProjectView::Grid) {
        drawProjectItemsGrid(items, m_thumbnailSize, actions);
    } else {
        drawProjectItemsList(items, actions);
    }

    if (items.empty()) {
        ImGui::TextDisabled(filtering ? "No matches" : "No materials loaded");
    }

    drawMaterialsContextMenu();
    ImGui::EndChild();
}

void EditorUI::drawTexturesTab(const ResourceStore &resources)
{
    const bool filtering = searchBar("texsearch", m_textureFilter);
    ImGui::Separator();

    std::vector<ProjectItem> items;
    for (uint32_t i = 1; i <= resources.textureCount(); ++i) {
        if (!passesOriginFilter(resources.textureOrigin(i))) {
            continue;
        }

        const ResourceStore::Texture   &texture = resources.texture(i);
        const ResourceStore::ImageInfo &info    = resources.imageInfo(texture.imageId);

        const std::string name = info.name.empty() ? "Texture " + std::to_string(i) : info.name;
        if (!matchesFilter(name, m_textureFilter)) {
            continue;
        }

        ProjectItem item;
        item.payload = i;

        item.label = std::filesystem::path(name).filename().string();
        item.badge = "TEX";

        if (const VkDescriptorSet set = texturePreview(resources, i)) {
            item.thumbnail = reinterpret_cast<ImTextureID>(set);
        }
        items.push_back(std::move(item));
    }

    ProjectItemActions actions;
    actions.isSelected = [this](const ProjectItem &item)
    {
        return m_selectionMode == SelectionMode::Texture && m_selectedTexture == item.payload;
    };
    actions.onClick = [this](const ProjectItem &item)
    {
        m_selectionMode   = SelectionMode::Texture;
        m_selectedTexture = item.payload;
        m_selectedNode    = {};
        m_selectedSlot    = 0;
    };
    actions.onActivate = actions.onClick;
    actions.onDragSource = [this, &resources](const ProjectItem &item)
    {
        beginTextureDrag(resources, item.payload);
    };

    ImGui::BeginChild("TextureList");
    if (m_projectView == ProjectView::Grid) {
        drawProjectItemsGrid(items, m_thumbnailSize, actions);
    } else {
        drawProjectItemsList(items, actions);
    }

    if (items.empty()) {
        ImGui::TextDisabled(filtering ? "No matches" : "No textures loaded");
    }
    ImGui::EndChild();
}

void EditorUI::drawHierarchy(const Scene &scene, const GeometryStore &geometry)
{
    if (ImGui::Button("Create")) {
        ImGui::OpenPopup("hierarchy_create");
    }
    if (ImGui::BeginPopup("hierarchy_create")) {
        drawCreateMenuItems({});
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(m_selectionMode != SelectionMode::Node || m_selectedSlot == 0);
    if (ImGui::Button("Delete")) {
        deleteNode(m_selectedNode);
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    if (ImGui::BeginChild("hierarchy", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        for (uint32_t id = scene.rootNodeId(); id != 0; ) {
            const uint32_t next = scene.getNode(id).nextSiblingId;
            drawHierarchyNode(scene, geometry, id);
            id = next;
        }

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsAnyItemHovered()) {
            clearSelection();
        }

        if (ImGui::BeginPopupContextWindow("hierarchy_context",
                                           ImGuiPopupFlags_MouseButtonRight |
                                           ImGuiPopupFlags_NoOpenOverItems)) {
            drawCreateMenuItems({});
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void EditorUI::drawInspector(const Scene &scene, const GeometryStore &geometry, const ResourceStore &resources)
{
    // 1. Nothing selected
    if (m_selectionMode == SelectionMode::None) {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::TextDisabled("Select a node or asset to inspect");
        return;
    }

    // 2. Inspecting a Material Asset directly
    if (m_selectionMode == SelectionMode::Material) {
        if (m_selectedMaterial == 0 || m_selectedMaterial > resources.materialCount()) {
            ImGui::TextDisabled("Invalid material selected");
            return;
        }
        drawMaterialSection(resources, m_selectedMaterial);
        return;
    }

    // 3. Inspecting a Texture picked in the Textures tab
    if (m_selectionMode == SelectionMode::Texture) {
        if (m_selectedTexture == 0 || m_selectedTexture > resources.textureCount()) {
            ImGui::TextDisabled("Invalid texture selected");
            return;
        }
        drawTextureSection(resources, m_selectedTexture);
        return;
    }

    // 4. Inspecting a Scene Node
    if (m_selectionMode == SelectionMode::Node) {
        if (m_selectedSlot == 0) return;

        const Node &node = scene.getNode(m_selectedSlot);

        // Every widget below edits this copy. If anything changed, the copy's
        // snapshot goes out as one Modify at the end
        // the scene itself is never touched from here
        Node        edited   = node;
        bool        changed  = false;
        const char *editName = "Edit Node";

        if (m_eulerOwner != m_selectedNode || node.getRotation() != m_eulerSource) {
            m_eulerOwner  = m_selectedNode;
            m_eulerSource = node.getRotation();
            glm::vec3 radians(0.0f);
            glm::extractEulerAngleYXZ(glm::mat4_cast(m_eulerSource), radians.y, radians.x, radians.z);
            m_eulerDegrees = glm::degrees(radians);
        }
        char nameBuffer[128];
        std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", node.name.c_str());
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##nodename", "Name", nameBuffer, sizeof(nameBuffer))) {
            edited.name = nameBuffer;
            changed     = true;
            editName    = "Rename Node";
        }

        ImGui::Text("Node %u", m_selectedSlot);
        if (ImGui::BeginItemTooltip()) {
            ImGui::Text("Guid %s", m_selectedNode.toString().c_str());
            ImGui::EndTooltip();
        }
        if (node.meshId != 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("| mesh %u", node.meshId);
        }

        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            beginProperties("transform_props");

            glm::vec3 translation = edited.getTranslation();
            if (vec3Control("Position", translation, 0.0f, 0.05f)) {
                edited.setTranslation(translation);
                changed  = true;
                editName = "Move";
            }

            if (vec3Control("Rotation", m_eulerDegrees, 0.0f, 0.5f)) {
                const glm::vec3 r = glm::radians(m_eulerDegrees);
                const glm::quat q = glm::quat_cast(glm::eulerAngleYXZ(r.y, r.x, r.z));
                edited.setRotation(q);
                // What the node will hold once this applies -- so next frame
                // the cache recognises its own write and keeps the angles.
                m_eulerSource = q;
                changed  = true;
                editName = "Rotate";
            }

            glm::vec3 scale = edited.getScale();
            if (vec3Control("Scale", scale, 1.0f, 0.01f)) {
                edited.setScale(scale);
                changed  = true;
                editName = "Scale";
            }

            endProperties();
        }

        // Before the mesh early-out below: a light node has no mesh, and
        // returning first would leave its panel empty.
        if (drawLightSection(edited)) {
            changed  = true;
            editName = "Edit Light";
        }

        if (changed) {
            submitModify(EditTarget::forNode(m_selectedNode), reflect::toBlob(edited), editName);
        }

        const uint32_t meshId = node.meshId;
        if (!geometry.meshAlive(meshId)) {
            return;
        }

        if (m_subMeshOwner != m_selectedNode) {
            m_subMeshOwner    = m_selectedNode;
            m_selectedSubMesh = 0;
        }

        const Mesh &mesh = geometry.mesh(meshId);
        drawMeshSection(m_selectedNode, mesh, resources);

        if (m_selectedSubMesh < mesh.subMeshes.size()) {
            drawMaterialSection(resources, mesh.subMeshes[m_selectedSubMesh].materialId,
                                m_selectedNode, static_cast<uint32_t>(m_selectedSubMesh));
        }
    }
}

void EditorUI::invalidateTexturePreview(uint32_t textureId)
{
    const auto it = m_previewSets.find(textureId);
    if (it == m_previewSets.end()) {
        return;
    }
    if (it->second) {
        ImGui_ImplVulkan_RemoveTexture(it->second);
    }
    m_previewSets.erase(it);
}


bool EditorUI::initialize(SDL_Window *window, VulkanContext &ctx,
                          VkFormat colorFormat, VkFormat depthFormat,
                          uint32_t minImageCount, uint32_t imageCount,
                          VkQueue graphicsQueue, uint32_t graphicsQueueFamily)
{
    constexpr uint32_t PoolSets = MaxTextures + 16;

    VkDescriptorPoolSize poolSize
    {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = PoolSets
    };
    VkDescriptorPoolCreateInfo poolInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = PoolSets,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize
    };
    if (vkCreateDescriptorPool(ctx.device(), &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        showError("EditorUI: failed to create the descriptor pool");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();  applyTheme();

    ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;



    io.IniFilename = "editor_layout.ini";

    if (!ImGui_ImplSDL3_InitForVulkan(window)) {
        showError("EditorUI: ImGui_ImplSDL3_InitForVulkan failed");
        return false;
    }
    ImGui_ImplVulkan_LoadFunctions(
        VK_API_VERSION_1_3,
        [](const char *name, void *userData) {
            return vkGetInstanceProcAddr(static_cast<VkInstance>(userData), name);
        },
        ctx.instance());

    VkPipelineRenderingCreateInfo renderingInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
        .depthAttachmentFormat = depthFormat
    };

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = ctx.instance();
    initInfo.PhysicalDevice = ctx.physical();
    initInfo.Device = ctx.device();
    initInfo.QueueFamily = graphicsQueueFamily;
    initInfo.Queue = graphicsQueue;
    initInfo.DescriptorPool = m_pool;
    initInfo.MinImageCount = minImageCount;
    initInfo.ImageCount = imageCount;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineRenderingCreateInfo = renderingInfo;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        showError("EditorUI: ImGui_ImplVulkan_Init failed");
        return false;
    }

    m_initialized = true;
    m_currentAssetPath = ASSET_DIR;
    return true;
}

void EditorUI::shutdown(VulkanContext &ctx)
{
    if (!m_initialized) {
        return;
    }
    vkDeviceWaitIdle(ctx.device());

    m_previewSets.clear();

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (m_pool) {
        vkDestroyDescriptorPool(ctx.device(), m_pool, nullptr);
        m_pool = nullptr;
    }
    m_initialized = false;
}

// Frame

void EditorUI::processEvent(const SDL_Event &event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
}

bool EditorUI::wantsMouse() const    { return ImGui::GetIO().WantCaptureMouse; }
bool EditorUI::wantsKeyboard() const { return ImGui::GetIO().WantCaptureKeyboard; }

void EditorUI::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGuizmo::BeginFrame();
}

void EditorUI::record(VkCommandBuffer cmd)
{
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

void EditorUI::selectNode(Guid node, uint32_t subMeshIndex)
{
    if (node.isNull()) {
        clearSelection();
        return;
    }

    m_selectionMode = SelectionMode::Node;
    m_selectedNode  = node;
    // Resolved by the next build(). A node created this frame is not in the
    // scene until its command has been applied.
    m_selectedSlot  = 0;

    // Force the inspector to show the exact submesh we clicked on
    m_subMeshOwner    = node;
    m_selectedSubMesh = subMeshIndex;
}

void EditorUI::selectMaterial(uint32_t materialId)
{
    if (materialId == 0) {
        clearSelection();
        return;
    }

    m_selectionMode    = SelectionMode::Material;
    m_selectedMaterial = materialId;
    m_selectedNode     = {};
    m_selectedSlot     = 0;
    m_selectedTexture  = 0;
}

void EditorUI::clearSelection()
{
    m_selectionMode    = SelectionMode::None;
    m_selectedNode     = {};
    m_selectedSlot     = 0;
    m_selectedMaterial = 0;
    m_selectedTexture  = 0;
}



void EditorUI::drawHierarchyNode(const Scene &scene, const GeometryStore &geometry, uint32_t nodeId)
{
    if (!scene.isAlive(nodeId)) {
        return;
    }

    const Node &node = scene.getNode(nodeId);
    const Guid  guid = node.guid();

    const uint32_t firstChild = node.firstChildId;
    const uint32_t meshId = node.meshId;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (firstChild == 0) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (m_selectionMode == SelectionMode::Node && guid == m_selectedNode) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    std::string label = node.name;
    if (label.empty() && geometry.meshAlive(meshId)) {
        label = geometry.mesh(meshId).name;
    }
    if (label.empty()) {
        label = meshId != 0 ? "Mesh" : "Node";
    }
    label += " (" + std::to_string(nodeId) + ")";

    void *treeId = reinterpret_cast<void *>(static_cast<uintptr_t>(nodeId));
    const bool renaming = isRenamingNode(guid);

    const bool open = renaming
        ? ImGui::TreeNodeEx(treeId, flags, "%s", "")
        : ImGui::TreeNodeEx(treeId, flags, "%s", label.c_str());

    if (renaming) {
        ImGui::SameLine();
        if (renameField("##noderename")) {
            Node renamed = node;
            renamed.name = m_renameBuffer;
            submitModify(EditTarget::forNode(guid), reflect::toBlob(renamed), "Rename Node");
            cancelRename();
        }
    } else {
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            selectNode(guid, 0);
        }

        if (const uint32_t droppedMaterial = acceptMaterialDrop()) {
            assignMaterial(guid, EditorCommand::kAllSubMeshes, droppedMaterial);
        }
        drawNodeContextMenu(guid, node.name);
    }

    if (open && firstChild != 0) {
        for (uint32_t child = firstChild; child != 0; ) {
            const uint32_t next = scene.getNode(child).nextSiblingId;
            drawHierarchyNode(scene, geometry, child);
            child = next;
        }
        ImGui::TreePop();
    }
}