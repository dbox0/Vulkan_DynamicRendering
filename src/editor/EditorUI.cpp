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
#include "../common/errors.h"
#include <glm/gtx/euler_angles.hpp>

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

void EditorUI::build(Scene &scene, const GeometryStore &geometry, ResourceStore &resources)
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
        drawInspector(scene, geometry, resources);
    }
    ImGui::End();

    // Requires MenuBar flag for our Tabs
    if (ImGui::Begin("Project", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar)) {
        drawProjectPanel(resources);
    }
    ImGui::End();

    ImGui::End(); // End EditorDockSpaceWindow
}

void EditorUI::drawProjectPanel(ResourceStore &resources)
{
    // Draw the Menu Bar acting as Tabs
    if (ImGui::BeginMenuBar()) {
        if (ImGui::MenuItem("Assets", nullptr, m_projectTab == ProjectTab::Assets)) {
            m_projectTab = ProjectTab::Assets;
        }
        if (ImGui::MenuItem("Materials", nullptr, m_projectTab == ProjectTab::Materials)) {
            m_projectTab = ProjectTab::Materials;
        }
        ImGui::EndMenuBar();
    }

    // Tab 1: File Browser
    if (m_projectTab == ProjectTab::Assets) {

        // Navigation bar
        ImGui::TextDisabled("Current Path:");
        ImGui::SameLine();
        ImGui::TextUnformatted(m_currentAssetPath.string().c_str());

        // Back button (disable if we are at the root ASSET_DIR)
        std::filesystem::path rootPath = std::filesystem::absolute(ASSET_DIR);
        std::filesystem::path currentAbs = std::filesystem::absolute(m_currentAssetPath);

        if (currentAbs != rootPath) {
            if (ImGui::Button("<- Up")) {
                m_currentAssetPath = m_currentAssetPath.parent_path();
            }
            ImGui::Separator();
        }

        // List files and directories
        ImGui::BeginChild("AssetList");
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(m_currentAssetPath, ec)) {
            const bool isDir = entry.is_directory();
            std::string filename = entry.path().filename().string();

            // Basic icons
            std::string label = (isDir ? "[DIR]  " : "[FILE] ") + filename;

            if (ImGui::Selectable(label.c_str())) {
                if (isDir) {
                    m_currentAssetPath = entry.path();
                } else {
                    // TODO: Handle double clicking files (load material, load texture, etc.)
                }
            }
        }
        ImGui::EndChild();
    }

    // Tab 2: Materials
   else if (m_projectTab == ProjectTab::Materials) {
        ImGui::BeginChild("MaterialList");

        // Define our grid cell sizes
        const float thumbnailSize = 64.0f;
        const float padding = 16.0f;
        const float cellSize = thumbnailSize + padding;

        // Calculate how many columns we can fit in the panel's current width
        float panelWidth = ImGui::GetContentRegionAvail().x;
        int columnCount = std::max(1, static_cast<int>(panelWidth / cellSize));

        if (ImGui::BeginTable("MaterialGrid", columnCount)) {
            for (uint32_t i = 1; i <= resources.materialCount(); ++i) {
                ImGui::TableNextColumn();
                ImGui::PushID(i);

                const auto& mat = resources.material(i);
                bool isSelected = (m_selectionMode == SelectionMode::Material && m_selectedMaterial == i);

                // 1. Draw a highlight box behind the thumbnail if selected
                ImVec2 cursorPos = ImGui::GetCursorScreenPos();
                if (isSelected) {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->AddRectFilled(
                        ImVec2(cursorPos.x - 4.0f, cursorPos.y - 4.0f),
                        ImVec2(cursorPos.x + thumbnailSize + 4.0f, cursorPos.y + thumbnailSize + ImGui::GetTextLineHeight() * 2.0f),
                        ImGui::GetColorU32(ImGuiCol_ButtonActive),
                        4.0f // Rounding
                    );
                }

                // 2. Draw the Thumbnail (Image or Color)
                bool clicked = false;

                if (mat.baseColorTexture != 0 && mat.baseColorTexture <= resources.textureCount()) {
                    // It has a texture, grab the preview descriptor set
                    VkDescriptorSet thumbSet = texturePreview(resources, mat.baseColorTexture);
                    if (thumbSet) {
                        // ImageButton provides a nice clickable frame
                        clicked = ImGui::ImageButton("##thumb",
                                                     reinterpret_cast<ImTextureID>(thumbSet),
                                                     ImVec2(thumbnailSize, thumbnailSize));
                    }
                } else {
                    // It's a solid color material, render a clickable color block
                    ImVec4 color(mat.baseColorFactor.r, mat.baseColorFactor.g, mat.baseColorFactor.b, 1.0f);
                    clicked = ImGui::ColorButton("##thumb",
                                                 color,
                                                 ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                                                 ImVec2(thumbnailSize, thumbnailSize));
                }

                // Update selection if the thumbnail was clicked
                if (clicked) {
                    m_selectedMaterial = i;
                    m_selectionMode = SelectionMode::Material;
                }

                // 3. Draw the material name underneath
                std::string name = mat.name.empty() ? "Material " + std::to_string(i) : mat.name;

                // Push text wrap pos so long names wrap within the cell
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
                ImGui::TextUnformatted(name.c_str());
                ImGui::PopTextWrapPos();

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
    }
}

void EditorUI::drawHierarchy(Scene &scene, const GeometryStore &geometry)
{
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    if (ImGui::BeginChild("hierarchy", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        for (uint32_t id = scene.rootNodeId(); id != 0; ) {
            const uint32_t next = scene.getNode(id).nextSiblingId;
            drawHierarchyNode(scene, geometry, id);
            id = next;
        }

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsAnyItemHovered()) {
            m_selectedNode = 0;
        }
    }
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsAnyItemHovered()) {
        m_selectedNode = 0;
        m_selectionMode = SelectionMode::None; // Clear selection
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
        drawMeshSection(mesh, resources);

        if (m_selectedSubMesh < mesh.subMeshes.size()) {
            drawMaterialSection(resources, mesh.subMeshes[m_selectedSubMesh].materialId);
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
    // ImGui allocates one combined image sampler set per texture it binds:
    // the font atlas, plus one per texture preview. Previews are cached per
    // texture ID, so MaxTextures + headroom can never be exhausted. A few
    // thousand descriptors is kilobytes -- not worth an eviction scheme.
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

void EditorUI::clearSelection()
{
    m_selectionMode = SelectionMode::None;
    m_selectedNode = 0;
    m_selectedMaterial = 0;
}



void EditorUI::drawHierarchyNode(Scene &scene, const GeometryStore &geometry, uint32_t nodeId)
{
    // Re-fetched on every use rather than held: a reference taken here would
    // dangle the moment NodeWorld's vector reallocates, and it will once
    // creating nodes from the editor is possible.
    Node &node = scene.getNode(nodeId);

    const uint32_t firstChild = node.firstChildId;
    const uint32_t meshId = node.meshId;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (firstChild == 0) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (nodeId == m_selectedNode) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    // Nodes have no name field yet. Falling back to the mesh name is enough
    // to navigate a loaded glTF; adding std::string Node::name is the real
    // fix and costs nothing but memory.
    std::string label;
    if (geometry.meshAlive(meshId)) {
        label = geometry.mesh(meshId).name;
    }
    if (label.empty()) {
        label = meshId != 0 ? "Mesh" : "Node";
    }
    label += " (" + std::to_string(nodeId) + ")";

    const bool open = ImGui::TreeNodeEx(reinterpret_cast<void *>(static_cast<uintptr_t>(nodeId)),
                                        flags, "%s", label.c_str());

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        m_selectedNode = nodeId;
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