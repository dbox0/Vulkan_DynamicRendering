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

#include "../render/VulkanContext.h"
#include "../render/GeometryStore.h"
#include "../render/ResourceStore.h"
#include "../common/constants.h"
#include "../scene/Scene.h"
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

    // ---------------------------------------------------------------------
    // Palette
    //
    // Almost everything is neutral.
    // Blue is reserved for interaction.
    // ---------------------------------------------------------------------

    const ImVec4 bg          = ImVec4(0.060f, 0.060f, 0.065f, 1.0f);
    const ImVec4 bgDark      = ImVec4(0.045f, 0.045f, 0.050f, 1.0f);
    const ImVec4 panel       = ImVec4(0.075f, 0.075f, 0.080f, 1.0f);

    const ImVec4 control     = ImVec4(0.110f, 0.110f, 0.115f, 1.0f);
    const ImVec4 hover       = ImVec4(0.145f, 0.145f, 0.150f, 1.0f);
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

    // ---------------------------------------------------------------------
    // Text
    // ---------------------------------------------------------------------

    c[ImGuiCol_Text]         = text;
    c[ImGuiCol_TextDisabled] = textDim;

    // ---------------------------------------------------------------------
    // Windows / panels
    // ---------------------------------------------------------------------

    c[ImGuiCol_WindowBg]     = bg;
    c[ImGuiCol_ChildBg]      = bgDark;
    c[ImGuiCol_PopupBg]      = panel;

    c[ImGuiCol_Border]       = border;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

    // ---------------------------------------------------------------------
    // Inputs / controls
    // ---------------------------------------------------------------------

    c[ImGuiCol_FrameBg]        = control;
    c[ImGuiCol_FrameBgHovered] = hover;
    c[ImGuiCol_FrameBgActive]  = active;

    // ---------------------------------------------------------------------
    // Title bars
    //
    // IMPORTANT: no blue title bars.
    // ---------------------------------------------------------------------

    c[ImGuiCol_TitleBg]          = bgDark;
    c[ImGuiCol_TitleBgActive]    = panel;
    c[ImGuiCol_TitleBgCollapsed] = bgDark;

    c[ImGuiCol_MenuBarBg] = bgDark;

    // ---------------------------------------------------------------------
    // Scrollbars
    // ---------------------------------------------------------------------

    c[ImGuiCol_ScrollbarBg]     = bgDark;
    c[ImGuiCol_ScrollbarGrab]   = control;
    c[ImGuiCol_ScrollbarGrabHovered] = hover;
    c[ImGuiCol_ScrollbarGrabActive]  = active;

    // ---------------------------------------------------------------------
    // Buttons
    //
    // Neutral until interacted with.
    // ---------------------------------------------------------------------

    c[ImGuiCol_Button]        = control;
    c[ImGuiCol_ButtonHovered] = hover;
    c[ImGuiCol_ButtonActive]  = active;

    // ---------------------------------------------------------------------
    // Headers / tree nodes
    // ---------------------------------------------------------------------

    c[ImGuiCol_Header]        = control;
    c[ImGuiCol_HeaderHovered] = hover;
    c[ImGuiCol_HeaderActive]  = active;

    // ---------------------------------------------------------------------
    // Checkboxes / sliders
    //
    // Blue only appears where it communicates state.
    // ---------------------------------------------------------------------

    c[ImGuiCol_CheckMark]      = blue;
    c[ImGuiCol_SliderGrab]     = blueSoft;
    c[ImGuiCol_SliderGrabActive] = blue;

    // ---------------------------------------------------------------------
    // Separators
    // ---------------------------------------------------------------------

    c[ImGuiCol_Separator]        = border;
    c[ImGuiCol_SeparatorHovered] = borderLight;
    c[ImGuiCol_SeparatorActive]  = blueSoft;

    // ---------------------------------------------------------------------
    // Resize grips
    // ---------------------------------------------------------------------

    c[ImGuiCol_ResizeGrip]        = control;
    c[ImGuiCol_ResizeGripHovered] = hover;
    c[ImGuiCol_ResizeGripActive]  = blue;

    // ---------------------------------------------------------------------
    // Tabs
    //
    // Mostly gray. Blue only communicates the active tab.
    // ---------------------------------------------------------------------

    c[ImGuiCol_Tab]                = bgDark;
    c[ImGuiCol_TabHovered]         = hover;
    c[ImGuiCol_TabSelected]        = panel;
    c[ImGuiCol_TabSelectedOverline] = blue;
    c[ImGuiCol_TabDimmed]          = bgDark;
    c[ImGuiCol_TabDimmedSelected]  = panel;
    c[ImGuiCol_TabDimmedSelectedOverline] = blueDim;

    // ---------------------------------------------------------------------
    // Tables
    // ---------------------------------------------------------------------

    c[ImGuiCol_TableHeaderBg]     = control;
    c[ImGuiCol_TableBorderStrong] = border;
    c[ImGuiCol_TableBorderLight]  = ImVec4(0.130f, 0.130f, 0.135f, 1.0f);

    c[ImGuiCol_TableRowBg]        = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]     = ImVec4(0.080f, 0.080f, 0.085f, 1.0f);

    // ---------------------------------------------------------------------
    // Selection / navigation
    // ---------------------------------------------------------------------

    c[ImGuiCol_TextSelectedBg] = blueDim;
    c[ImGuiCol_NavCursor]      = blue;

    // ---------------------------------------------------------------------
    // Drag & drop
    // ---------------------------------------------------------------------

    c[ImGuiCol_DragDropTarget] = blue;

    // ---------------------------------------------------------------------
    // Modal dimming
    // ---------------------------------------------------------------------

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

// The three-coloured-field control everyone recognises from Unity/Unreal.
// Clicking the X/Y/Z button resets that axis.
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

    // Divides the remaining width into three equal fields, accounting for the
    // buttons we are about to insert.
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
// ---------------------------------------------------------------------------

void EditorUI::build(Scene &scene, const GeometryStore &geometry, ResourceStore &resources,
                     const Camera &camera, uint32_t width, uint32_t height)
{
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

    // If there is no layout loaded from INI, build the default Unity-style layout
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

    handleShortcuts();
    drawGizmo(scene, camera, width, height);
}

namespace
{
    // Case-insensitive substring test. ASCII-only, which is all a filename or
    // a material name needs. ImGuiTextFilter would do the multi-term and
    // -exclude syntax for free, but it is case SENSITIVE -- the wrong default
    // for a file browser, and it would drag imgui.h into EditorUI.h.
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
    // Exactly the shape the Project panel needs to draw one cell, in either
    // view, for either tab. Lives here rather than in EditorUI.h because of
    // the ImTextureID / ImVec4: the header stays imgui-free.
    struct ProjectItem
    {
        std::string label;
        ImTextureID thumbnail = 0;                      // 0 -> use colour
        ImVec4      color{ 0.3f, 0.3f, 0.32f, 1.0f };
        const char *badge = nullptr;                    // "DIR", "GLTF", "MAT"
        uint32_t    payload = 0;                        // material id, or index into the entry list
    };

    // Below this the grid stops making sense -- the label is wider than the
    // tile and everything wraps. That is where list view takes over.
    constexpr float ListThreshold = 26.0f;

    struct ProjectItemActions
    {
        std::function<bool(const ProjectItem &)> isSelected;
        std::function<void(const ProjectItem &)> onClick;
        std::function<void(const ProjectItem &)> onActivate;     // double click
        std::function<void(const ProjectItem &)> onDragSource;   // called right after the widget
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
                // NoDragDrop, or ImGui's own colour payload competes with the
                // asset payload we attach below.
                clicked = ImGui::ColorButton("##thumb", item.color,
                                             ImGuiColorEditFlags_NoTooltip |
                                             ImGuiColorEditFlags_NoDragDrop,
                                             ImVec2(thumbSize, thumbSize));
            }

            // Must follow the widget immediately: BeginDragDropSource with
            // default flags binds to the last item submitted.
            if (actions.onDragSource) {
                actions.onDragSource(item);
            }

            const bool activated = ImGui::IsItemHovered() &&
                                   ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

            if (item.badge && !item.thumbnail) {
                const ImVec2 textSize = ImGui::CalcTextSize(item.badge);
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(origin.x + (thumbSize - textSize.x) * 0.5f,
                           origin.y + (thumbSize - textSize.y) * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_Text), item.badge);
            }

            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbSize);
            ImGui::TextUnformatted(item.label.c_str());
            ImGui::PopTextWrapPos();

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

    // Lowercased extension, so ".PNG" and ".png" classify the same.
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

void EditorUI::drawProjectPanel(ResourceStore &resources)
{
    if (ImGui::BeginMenuBar()) {
        if (ImGui::MenuItem("Assets", nullptr, m_projectTab == ProjectTab::Assets)) {
            m_projectTab = ProjectTab::Assets;
        }
        if (ImGui::MenuItem("Materials", nullptr, m_projectTab == ProjectTab::Materials)) {
            m_projectTab = ProjectTab::Materials;
        }

        // Right-aligned, and it is the view switch as well as the zoom: at the
        // minimum the tiles would be smaller than their own labels, so that end
        // of the slider is list view.
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

    if (m_projectTab == ProjectTab::Assets) {
        drawAssetsTab();
    } else {
        drawMaterialsTab(resources);
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
        item.label   = filename;
        item.payload = static_cast<uint32_t>(m_assetEntries.size());

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
    actions.onActivate = [this](const ProjectItem &item)
    {
        const std::filesystem::path &path = m_assetEntries[item.payload];
        std::error_code dirEc;
        if (std::filesystem::is_directory(path, dirEc)) {
            m_currentAssetPath = path;
            return;
        }

        const std::string ext = lowerExtension(path);
        if (ext == ".gltf" || ext == ".glb") {
            EditorCommand cmd;
            cmd.kind = EditorCommand::Kind::LoadModel;
            cmd.path = path;
            m_commands.push_back(cmd);
        } else if (ext == ".mat") {
            EditorCommand cmd;
            cmd.kind = EditorCommand::Kind::LoadMaterial;
            cmd.path = path;
            m_commands.push_back(cmd);
        }
    };
    actions.onDragSource = [this](const ProjectItem &item)
    {
        const std::filesystem::path &path = m_assetEntries[item.payload];
        if (isImageExtension(lowerExtension(path))) {
            beginAssetDrag(path);
        }
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
    // NoOpenOverItems so right-clicking a file does not get the create menu --
    // that space is reserved for a per-item menu (rename, delete, reveal).
    if (!ImGui::BeginPopupContextWindow("assetcontext",
                                        ImGuiPopupFlags_MouseButtonRight |
                                        ImGuiPopupFlags_NoOpenOverItems)) {
        return;
    }

    if (ImGui::MenuItem("New Folder")) {
        EditorCommand cmd;
        cmd.kind = EditorCommand::Kind::CreateDirectory;
        cmd.path = m_currentAssetPath / "New Folder";
        m_commands.push_back(cmd);
    }

    if (ImGui::MenuItem("New Material")) {
        EditorCommand cmd;
        cmd.kind = EditorCommand::Kind::CreateMaterial;
        cmd.path = m_currentAssetPath / "New Material.mat";
        m_commands.push_back(cmd);
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

    // No path: created in memory, MaterialInfo::sourcePath stays empty, and
    // the inspector offers "Save As" rather than "Save".
    if (ImGui::MenuItem("New Material")) {
        EditorCommand cmd;
        cmd.kind = EditorCommand::Kind::CreateMaterial;
        m_commands.push_back(cmd);
    }

    ImGui::EndPopup();
}

void EditorUI::drawMaterialsTab(ResourceStore &resources)
{
    const bool filtering = searchBar("matsearch", m_materialFilter);
    ImGui::Separator();

    std::vector<ProjectItem> items;
    for (uint32_t i = 1; i <= resources.materialCount(); ++i) {
        const Material &mat = resources.material(i);

        const std::string name = mat.name.empty() ? "Material " + std::to_string(i) : mat.name;
        if (!matchesFilter(name, m_materialFilter)) {
            continue;
        }

        ProjectItem item;
        item.payload = i;
        item.label   = resources.materialInfo(i).dirty ? name + " *" : name;
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

void EditorUI::drawHierarchy(Scene &scene, const GeometryStore &geometry)
{
    if (ImGui::Button("Create")) {
        ImGui::OpenPopup("hierarchy_create");
    }
    if (ImGui::BeginPopup("hierarchy_create")) {
        drawCreateMenuItems(0);
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(m_selectionMode != SelectionMode::Node || m_selectedNode == 0);
    if (ImGui::Button("Delete")) {
        deleteNode(m_selectedNode);
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    if (ImGui::BeginChild("hierarchy", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        // next is read before the row is drawn: the row can queue a delete, and
        // after that its sibling link is the only way out of the loop.
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
            drawCreateMenuItems(0);
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void EditorUI::drawInspector(Scene &scene, const GeometryStore &geometry, ResourceStore &resources)
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
        // This existing function already does all the heavy lifting for texture previews!
        drawMaterialSection(resources, m_selectedMaterial);
        return;
    }

    // 3. Inspecting a Scene Node (Original logic goes here)
    if (m_selectionMode == SelectionMode::Node) {
        if (m_selectedNode == 0) return;

        Node &node = scene.getNode(m_selectedNode);

        if (m_eulerOwner != m_selectedNode) {
            m_eulerOwner = m_selectedNode;
            glm::vec3 radians(0.0f);
            glm::extractEulerAngleYXZ(glm::mat4_cast(node.getRotation()), radians.y, radians.x, radians.z);
            m_eulerDegrees = glm::degrees(radians);
        }

        if (!node.name.empty()) {
            ImGui::TextUnformatted(node.name.c_str());
            ImGui::SameLine();
        }

        ImGui::Text("Node %u", m_selectedNode);
        if (node.meshId != 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("| mesh %u", node.meshId);
        }

        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            beginProperties("transform_props");

            glm::vec3 translation = node.getTranslation();
            if (vec3Control("Position", translation, 0.0f, 0.05f)) {
                node.setTranslation(translation);
            }

            if (vec3Control("Rotation", m_eulerDegrees, 0.0f, 0.5f)) {
                const glm::vec3 r = glm::radians(m_eulerDegrees);
                node.setRotation(glm::quat_cast(glm::eulerAngleYXZ(r.y, r.x, r.z)));
            }

            glm::vec3 scale = node.getScale();
            if (vec3Control("Scale", scale, 1.0f, 0.01f)) {
                node.setScale(scale);
            }

            endProperties();
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

    // Preview sets die with the pool below; just forget the handles.
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

void EditorUI::selectNode(uint32_t nodeId, uint32_t subMeshIndex)
{
    if (nodeId == 0) {
        clearSelection();
        return;
    }

    m_selectionMode = SelectionMode::Node;
    m_selectedNode = nodeId;

    // Force the inspector to show the exact submesh we clicked on
    m_subMeshOwner = nodeId;
    m_selectedSubMesh = subMeshIndex;
}

void EditorUI::selectMaterial(uint32_t materialId)
{
    if (materialId == 0) {
        clearSelection();
        return;
    }

    // Node and material selection are the same slot: the inspector shows one
    // or the other, never both.
    m_selectionMode    = SelectionMode::Material;
    m_selectedMaterial = materialId;
    m_selectedNode     = 0;
}

void EditorUI::clearSelection()
{
    m_selectionMode = SelectionMode::None;
    m_selectedNode = 0;
    m_selectedMaterial = 0;
}



void EditorUI::drawHierarchyNode(Scene &scene, const GeometryStore &geometry, uint32_t nodeId)
{
    if (!scene.isAlive(nodeId)) {
        return;
    }

    Node &node = scene.getNode(nodeId);

    const uint32_t firstChild = node.firstChildId;
    const uint32_t meshId = node.meshId;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (firstChild == 0) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (m_selectionMode == SelectionMode::Node && nodeId == m_selectedNode) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    // Node::name exists now, so an empty node and a second instance of the
    // same mesh are finally distinguishable. Mesh name is the fallback.
    std::string label = node.name;
    if (label.empty() && geometry.meshAlive(meshId)) {
        label = geometry.mesh(meshId).name;
    }
    if (label.empty()) {
        label = meshId != 0 ? "Mesh" : "Node";
    }
    label += " (" + std::to_string(nodeId) + ")";

    const bool open = ImGui::TreeNodeEx(reinterpret_cast<void *>(static_cast<uintptr_t>(nodeId)),
                                        flags, "%s", label.c_str());


    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        selectNode(nodeId, 0);
    }

    if (const uint32_t droppedMaterial = acceptMaterialDrop()) {
        assignMaterial(nodeId, EditorCommand::kAllSubMeshes, droppedMaterial);
    }
    drawNodeContextMenu(nodeId);

    if (open && firstChild != 0) {
        for (uint32_t child = firstChild; child != 0; ) {
            const uint32_t next = scene.getNode(child).nextSiblingId;
            drawHierarchyNode(scene, geometry, child);
            child = next;
        }
        ImGui::TreePop();
    }
}