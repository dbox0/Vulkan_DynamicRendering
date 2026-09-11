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
#include "../scene/Scene.h"
#include "../common/errors.h"
#include <glm/gtx/euler_angles.hpp>

#include <imgui_internal.h>     // for PushMultiItemsWidths
#include <glm/vec3.hpp>

void EditorUI::applyTheme()
{
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

    // --- shape ---
    // Unity's look is flat panels with slightly rounded controls. Rounding the
    // windows but not the panels is what keeps it from looking like a web app.
    style.WindowRounding    = 6.0f;
    style.ChildRounding     = 4.0f;
    style.FrameRounding     = 3.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 4.0f;

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize  = 1.0f;
    style.FrameBorderSize  = 0.0f;
    style.PopupBorderSize  = 1.0f;

    // Generous vertical padding is most of what makes a UI feel modern
    // rather than cramped.
    style.WindowPadding     = ImVec2(10.0f, 10.0f);
    style.FramePadding      = ImVec2(8.0f, 5.0f);
    style.CellPadding       = ImVec2(6.0f, 4.0f);
    style.ItemSpacing       = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.IndentSpacing     = 18.0f;
    style.ScrollbarSize     = 12.0f;
    style.GrabMinSize       = 10.0f;

    style.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_None;   // drop the collapse arrow

    // --- palette ---
    // Three greys: background, panel, control. Everything else is a tint of
    // one accent. Limiting yourself to that is what stops a custom theme
    // looking like a ransom note.
    const ImVec4 bg        = ImVec4(0.13f, 0.13f, 0.14f, 1.00f);
    const ImVec4 panel     = ImVec4(0.17f, 0.17f, 0.19f, 1.00f);
    const ImVec4 control   = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
    const ImVec4 hover     = ImVec4(0.27f, 0.27f, 0.31f, 1.00f);
    const ImVec4 active    = ImVec4(0.31f, 0.31f, 0.36f, 1.00f);
    const ImVec4 accent    = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    const ImVec4 accentDim = ImVec4(0.26f, 0.59f, 0.98f, 0.45f);
    const ImVec4 text      = ImVec4(0.88f, 0.88f, 0.90f, 1.00f);
    const ImVec4 textDim   = ImVec4(0.50f, 0.50f, 0.54f, 1.00f);
    const ImVec4 border    = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);

    colors[ImGuiCol_Text]                 = text;
    colors[ImGuiCol_TextDisabled]         = textDim;
    colors[ImGuiCol_WindowBg]             = bg;
    colors[ImGuiCol_ChildBg]              = panel;
    colors[ImGuiCol_PopupBg]              = panel;
    colors[ImGuiCol_Border]               = border;
    colors[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    colors[ImGuiCol_FrameBg]              = control;
    colors[ImGuiCol_FrameBgHovered]       = hover;
    colors[ImGuiCol_FrameBgActive]        = active;

    colors[ImGuiCol_TitleBg]              = panel;
    colors[ImGuiCol_TitleBgActive]        = panel;
    colors[ImGuiCol_TitleBgCollapsed]     = panel;
    colors[ImGuiCol_MenuBarBg]            = panel;

    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ScrollbarGrab]        = control;
    colors[ImGuiCol_ScrollbarGrabHovered] = hover;
    colors[ImGuiCol_ScrollbarGrabActive]  = active;

    colors[ImGuiCol_CheckMark]            = accent;
    colors[ImGuiCol_SliderGrab]           = accent;
    colors[ImGuiCol_SliderGrabActive]     = accent;

    colors[ImGuiCol_Button]               = control;
    colors[ImGuiCol_ButtonHovered]        = hover;
    colors[ImGuiCol_ButtonActive]         = active;

    colors[ImGuiCol_Header]               = accentDim;
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.26f, 0.59f, 0.98f, 0.60f);
    colors[ImGuiCol_HeaderActive]         = accent;

    colors[ImGuiCol_Separator]            = border;
    colors[ImGuiCol_SeparatorHovered]     = accentDim;
    colors[ImGuiCol_SeparatorActive]      = accent;

    colors[ImGuiCol_ResizeGrip]           = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ResizeGripHovered]    = accentDim;
    colors[ImGuiCol_ResizeGripActive]     = accent;

    colors[ImGuiCol_Tab]                  = panel;
    colors[ImGuiCol_TabHovered]           = hover;
    colors[ImGuiCol_TabSelected]          = control;
    colors[ImGuiCol_TabDimmed]            = panel;
    colors[ImGuiCol_TabDimmedSelected]    = control;

    colors[ImGuiCol_TableHeaderBg]        = panel;
    colors[ImGuiCol_TableBorderStrong]    = border;
    colors[ImGuiCol_TableBorderLight]     = border;
    colors[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt]        = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);

    colors[ImGuiCol_NavCursor]            = accent;

    // FONT -- the single biggest visual upgrade, and it needs an actual file.
    // The built-in Proggy is bitmap and looks dated at any size. Ship a TTF
    // next to the binary and load it here:
    //
    //   ImGuiIO &io = ImGui::GetIO();
    //   io.Fonts->AddFontFromFileTTF("assets/fonts/Inter-Regular.ttf", 16.0f);
    //
    // Inter, Roboto and Source Sans Pro are all open-licensed and look right.
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
    const ImVec2 buttonSize(lineHeight + 3.0f, lineHeight);

    // Divides the remaining width into three equal fields, accounting for the
    // buttons we are about to insert.
    ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth() - buttonSize.x * 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

    struct Axis { const char *name; ImVec4 base, hover, active; float *value; };
    const Axis axes[3]
    {
        { "X", ImVec4(0.72f, 0.24f, 0.28f, 1.0f), ImVec4(0.82f, 0.32f, 0.36f, 1.0f),
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

void EditorUI::build(Scene &scene, const GeometryStore &geometry)
{
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, 620), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoCollapse)) {
        drawHierarchy(scene, geometry);
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        drawInspector(scene);
    }
    ImGui::End();
}

void EditorUI::drawHierarchy(Scene &scene, const GeometryStore &geometry)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.60f, 1.0f));
    ImGui::TextUnformatted("HIERARCHY");
    ImGui::PopStyleColor();

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    if (ImGui::BeginChild("hierarchy", ImVec2(0, 260), ImGuiChildFlags_Borders)) {
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
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void EditorUI::drawInspector(Scene &scene)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.60f, 1.0f));
    ImGui::TextUnformatted("INSPECTOR");
    ImGui::PopStyleColor();

    if (m_selectedNode == 0) {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::TextDisabled("Select a node in the hierarchy");
        return;
    }

    Node &node = scene.getNode(m_selectedNode);

    if (m_eulerOwner != m_selectedNode) {
        m_eulerOwner = m_selectedNode;
        glm::vec3 radians(0.0f);
        glm::extractEulerAngleYXZ(glm::mat4_cast(node.getRotation()),
                                  radians.y, radians.x, radians.z);
        m_eulerDegrees = glm::degrees(radians);
    }

    ImGui::Text("Node %u", m_selectedNode);
    if (node.meshId != 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("| mesh %u", node.meshId);
    }

    ImGui::Dummy(ImVec2(0.0f, 2.0f));

    // DefaultOpen because a collapsed Transform is nobody's idea of useful.
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

        // Reset value 1.0 for scale -- resetting an axis to 0 collapses the
        // model and looks like a crash.
        glm::vec3 scale = node.getScale();
        if (vec3Control("Scale", scale, 1.0f, 0.01f)) {
            node.setScale(scale);
        }

        endProperties();
    }
}


bool EditorUI::initialize(SDL_Window *window, VulkanContext &ctx,
                          VkFormat colorFormat, VkFormat depthFormat,
                          uint32_t minImageCount, uint32_t imageCount,
                          VkQueue graphicsQueue, uint32_t graphicsQueueFamily)
{
    // Imgui allovcates one combined image samlpler per texture it binds.
    // Font is the only one for now until we preview textures.

    VkDescriptorPoolSize poolSize
    {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 16
    };
    VkDescriptorPoolCreateInfo poolInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 16,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize
    };
    if (vkCreateDescriptorPool(ctx.device(), &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        showError("EditorUI: failed to create the descriptor pool");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    applyTheme();

    ImGuiIO &io = ImGui::GetIO();
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
    return true;
}

void EditorUI::shutdown(VulkanContext &ctx)
{
    if (!m_initialized) {
        return;
    }
    vkDeviceWaitIdle(ctx.device());

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
    if (meshId != 0 && meshId <= geometry.meshCount()) {
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



