#ifndef ASSET_DIR
#define ASSET_DIR "./"
#endif

#include "EditorUI.h"
#include "EditorTheme.h"
#include "EditorWidgets.h"

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

using namespace editor::ui;

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
            ImGui::MenuItem("Post Process", nullptr, &m_showPostProcessWindow);
            ImGui::MenuItem("Environment", nullptr, &m_showEnvironmentWindow);
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
    drawPostProcessWindow();
    drawEnvironmentWindow();

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