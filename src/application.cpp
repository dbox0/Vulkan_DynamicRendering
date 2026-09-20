#include "application.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <iostream>
#include <string_view>

#include "assets/GltfLoader.h"
#include "assets/Mesh.h"
#include "assets/MaterialSerializer.h"
#include "assets/PrimitiveBuilder.h"
#include "common/errors.h"
#include "editor/EditorCommands.h"
#include "scene/Geometry/Node.h"
#include "scene/SceneTypes.h"
#include "assets/AssetTypes.h"
#include "editor/EditorWorld.h"
#include "editor/UndoHistory.h"
#include "scene/SceneSerializer.h"
#include "reflect/BinaryArchive.h"
#include "reflect/Reflection.h"
#include <glm/gtx/quaternion.hpp>

namespace
{
    // "New Material.mat" -> "New Material 1.mat" when taken. Creating a second
    // folder should not silently do nothing, and overwriting an existing .mat
    // because the default name collided would be worse.
    std::filesystem::path uniquePath(const std::filesystem::path &desired)
    {
        std::error_code ec;
        if (!std::filesystem::exists(desired, ec)) {
            return desired;
        }

        const std::filesystem::path parent    = desired.parent_path();
        const std::string           stem      = desired.stem().string();
        const std::string           extension = desired.extension().string();

        for (int suffix = 1; suffix < 1000; ++suffix) {
            std::filesystem::path candidate =
                parent / (stem + " " + std::to_string(suffix) + extension);
            if (!std::filesystem::exists(candidate, ec)) {
                return candidate;
            }
        }
        return desired;
    }
}

namespace
{

    class ScopedTransaction
    {
    public:
        ScopedTransaction(UndoHistory &history, const EditorUI &editor, std::string_view name)
            : m_history(history), m_editor(editor)
        {
            m_history.begin(name, m_editor.selection());
        }
        ~ScopedTransaction() { m_history.commit(m_editor.selection()); }

        ScopedTransaction(const ScopedTransaction &) = delete;
        ScopedTransaction &operator=(const ScopedTransaction &) = delete;

    private:
        UndoHistory    &m_history;
        const EditorUI &m_editor;
    };

    std::string nodeLabel(const Node &node)
    {
        return node.name.empty() ? std::string("Node") : node.name;
    }
}

Application::Application()

    : m_swapchain(m_ctx)
    , m_resources(m_ctx)
    , m_geometry(m_ctx)
    , m_renderer(m_ctx, m_swapchain, m_resources, m_geometry)
    , m_cache(ASSET_DIR)
{
}

bool Application::initializeWindow()
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        // No window exists yet, so this goes to stderr only.
        std::cerr << "[error] Unable to initialize SDL3: " << SDL_GetError() << std::endl;
        return false;
    }

    m_window = SDL_CreateWindow("Learning Vulkan", static_cast<int>(m_width), static_cast<int>(m_height),
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!m_window) {
        std::cerr << "[error] Error creating window: " << SDL_GetError() << std::endl;
        return false;
    }

    // From here on every subsystem can call showError() without holding the window.
    setErrorWindow(m_window);

    return true;
}

bool Application::initialize()
{
    if (!initializeWindow()) {
        return false;
    }

    registerSceneTypes();
    registerAssetTypes();
    if (const auto errors = reflect::validate(); !errors.empty()) {
        std::string message = "Reflection tables are incomplete:";
        for (const auto &error : errors) {
            message += "\n  " + error;
        }
        showError(message);
        return false;
    }

    if (!m_ctx.initialize(m_window, VulkanVersion)) {
        showError("Failed to initialize the Vulkan context");
        return false;
    }

    if (!m_swapchain.create(m_width, m_height)) {
        showError("Failed to create the swapchain");
        return false;
    }

    if (!m_resources.initialize()) {
        showError("Failed to initialize the resource store");
        return false;
    }

    if (!m_renderer.initialize(MaxDrawsPerFrame)) {
        showError("Failed to initialize the renderer");
        return false;
    }
    if (!(m_geometry.reserve(VertexBudgetBytes, IndexBudgetBytes))) {
        showError("Failed to allocate geometry buffers");
        return false;
    }
    m_scene.initialize(MaxNodes);

    if (!m_editor.initialize(m_window, m_ctx,
                             Swapchain::ColorFormat, Swapchain::DepthFormat,
                             2, m_swapchain.imageCount(),
                             m_ctx.gfxQueue(), m_ctx.gfxFamily())) {
        showError("Failed to initialize the editor UI");
        return false;
                             }

    // The editor edits the renderer's shadow state in place; nothing is copied
    // back, so there is no lag on a slider drag.
    m_editor.bindShadowSettings(m_renderer.shadowSettings(), m_renderer.sunDirection());
    m_editor.bindCullSettings(m_renderer.cullSettings(),m_renderer.cullStats());
    m_editor.bindTonemapSettings(m_renderer.tonemapSettings());
    m_editor.bindEnvironmentSettings(m_renderer.environmentSettings());
    m_editor.bindSkyboxSettings(m_renderer.skyboxSettings());
    m_editor.bindBloomSettings(m_renderer.bloomSettings());
    m_editor.bindHistory(m_history);
    m_editor.bindScenePath(m_scenePath);
    m_detector.initialize(m_scene, m_resources, m_geometry, m_history);

    return true;
}

bool Application::loadData(const std::filesystem::path &modelPath)
{
    GltfLoader loader(m_ctx, m_resources, m_geometry, m_scene, m_cache);
    if (!loader.load(modelPath)) {
        showError("Failed to load model: " + modelPath.string());
        return false;
    }

    if (!m_geometry.flushUploads()) {
        showError("Failed to upload geometry to device memory");
        return false;
    }

    if (!m_resources.commitTextureDescriptors()) {
        showError("Failed to upload the material buffer");
        return false;
    }

    std::cout << "Loaded " << m_geometry.meshCount() << " meshes, "
              << m_resources.materialCount() << " materials" << std::endl;
    return true;
}

void Application::run()
{
    m_running = true;

    const bool *keys = SDL_GetKeyboardState(nullptr);
    uint64_t prevTime = SDL_GetTicks();

    // I dont like this and I dont think this will stay here.
    // This is only here for the Editor UI: Inits Mats already in res/mat..
    // for a build without imgui this has to go

    for (const auto& entry : std::filesystem::recursive_directory_iterator(ASSET_DIR + std::string("materials/"))) {
        if (entry.is_regular_file() && entry.path().extension() == ".mat") {
            m_pendingAssets.push_back({ PendingAsset::Kind::Material, entry.path() });
        }
    }

    m_camera.lookAt(glm::vec3(0,0,0));
    while (m_running) {

        const uint64_t currentTime = SDL_GetTicks();
        const float deltaTime = static_cast<float>(currentTime - prevTime) / 1000;
        prevTime = currentTime;

        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            m_editor.processEvent(event);
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    m_running = false;
                    break;
                case SDL_EVENT_DROP_FILE:
                    if (event.drop.data) {
                        const std::filesystem::path dropped(event.drop.data);
                        m_pendingAssets.push_back({
                            dropped.extension() == ".mat" ? PendingAsset::Kind::Material
                                                          : PendingAsset::Kind::Model,
                            dropped });
                    }
                    break;

                case SDL_EVENT_WINDOW_RESIZED:
                    m_width  = static_cast<uint32_t>(event.window.data1);
                    m_height = static_cast<uint32_t>(event.window.data2);
                    m_swapchain.flagForRecreate();
                    break;

                default:
                    break;
            }
            const bool isMouse =
              event.type == SDL_EVENT_MOUSE_MOTION ||
              event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
              event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
              event.type == SDL_EVENT_MOUSE_WHEEL;

            const bool isKey =
                event.type == SDL_EVENT_KEY_DOWN ||
                event.type == SDL_EVENT_KEY_UP;

            const bool claimed = (isMouse && m_editor.wantsMouse()) ||
                                (isKey   && m_editor.wantsKeyboard());

            if (!claimed) {
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                    event.button.button == SDL_BUTTON_LEFT &&
                    !m_editor.gizmoCapturesMouse()) {
                    pickAt(event.button.x, event.button.y);
                }
                m_camera.handleInput(event, deltaTime);
            }
        }
        m_camera.Update(deltaTime);
        updateWindowTitle();
        if (!m_running) {
            break;
        }
        if (m_width == 0 || m_height == 0) {
            SDL_Delay(16);
            continue;
        }

        // NewFrame must not run on a frame that gets skipped above -- ImGui
        // asserts if NewFrame is called twice without a Render in between.

        m_editor.beginFrame();
        m_editor.setBloomMipCount(m_renderer.bloomMipCount());
        m_editor.build(m_scene, m_geometry, m_resources, m_camera, m_width, m_height);

        m_detector.beginFrame();
        applyEditorCommands();
        loadPendingAssets();
        m_world.collectGarbage(m_history);
        m_detector.endFrame();

        m_renderer.setSelection(m_scene.findNode(m_editor.selectedNode()));

        m_renderer.render(m_scene, m_camera, m_width, m_height,
                         [this](VkCommandBuffer cmd) { m_editor.record(cmd); });
    }
}

void Application::applyEditorCommands()
{
    m_detector.beginFrame();

    const std::vector<EditorCommand> &commands = m_editor.commands();
    if (commands.empty()) {
        return;
    }

    bool geometryAdded = false;
    const auto select = [this](uint32_t nodeId) {
        m_editor.selectNode(m_scene.getNode(nodeId).guid(), 0);
    };
    m_structurallyChanged.clear();
    bool historyStepped = false;
    // A failed save stops a New/Load queued behind it in the same frame
    // ("Save" in the unsaved-changes prompt), so nothing unsaved is dropped.
    bool sceneSaveFailed = false;
    const auto isStale = [&](const EditTarget &target) {
        return historyStepped ||
               (target.kind == EditTarget::Kind::Node &&
                std::find(m_structurallyChanged.begin(), m_structurallyChanged.end(),
                          target.node) != m_structurallyChanged.end());
    };

    for (const EditorCommand &cmd : commands) {
        if (const std::string error = m_editChecker.check(cmd); !error.empty()) {
            fatalError("Editor command stream: " + error);
        }

        switch (cmd.kind) {

        // ---- property edits: one gesture, one undo step ------------------
        case EditorCommand::Kind::BeginEdit:
            m_history.begin(cmd.name, m_editor.selection());
            break;

        case EditorCommand::Kind::EndEdit:
            m_history.commit(m_editor.selection());
            break;

        case EditorCommand::Kind::Modify:
        {
            if (isStale(cmd.target)) {
                break;
            }
            if (!m_history.touch(cmd.target)) {
                break;
            }
            if (!m_world.apply(cmd.target, cmd.snapshot)) {
                fatalError("Modify: snapshot did not apply to a live target");
            }
            break;
        }

        // ---- history --------------------------------------------------------
        case EditorCommand::Kind::Undo:
            applyHistoryStep(m_history.undo(), "Undo");
            historyStepped = true;
            break;

        case EditorCommand::Kind::Redo:
            applyHistoryStep(m_history.redo(), "Redo");
            historyStepped = true;
            break;


        case EditorCommand::Kind::CreateEmpty:
        {
            ScopedTransaction tx(m_history, m_editor, "Create Empty");
            const uint32_t nodeId = m_scene.createNode(m_scene.findNode(cmd.parent), "Empty");
            if (!nodeId) {
                std::cerr << "[warn] Node budget exhausted" << std::endl;
                break;
            }
            m_history.created(m_scene.getNode(nodeId).guid());
            break;
        }

        case EditorCommand::Kind::CreateLight:
        {
            ScopedTransaction tx(m_history, m_editor, "Create Directional Light");
            const uint32_t nodeId = m_scene.createNode(m_scene.findNode(cmd.parent), "Directional Light");
            if (!nodeId) {
                std::cerr << "[warn] Node budget exhausted" << std::endl;
                break;
            }
            Node &node = m_scene.getNode(nodeId);
            node.lightType = LightType::Directional;

            // Placed above the origin and aimed down at an angle
            node.setTranslation(glm::vec3(0.0f, 4.0f, 0.0f));
            node.setRotation(glm::quatLookAt(
                glm::normalize(glm::vec3(0.3f, -1.0f, -0.5f)), glm::vec3(0.0f, 1.0f, 0.0f)));

            m_history.created(node.guid());
            select(nodeId);
            break;
        }

        case EditorCommand::Kind::CreatePrimitive:
        {
            const uint32_t meshId = buildPrimitive(m_geometry, cmd.primitive,
                                                   m_resources.defaultMaterialId());
            if (!meshId) {
                std::cerr << "[warn] Geometry budget exhausted" << std::endl;
                break;
            }
            geometryAdded = true;

            ScopedTransaction tx(m_history, m_editor,
                                 std::string("Create ") + primitiveName(cmd.primitive));
            const uint32_t nodeId = m_scene.createNode(m_scene.findNode(cmd.parent),
                                                       primitiveName(cmd.primitive), meshId);
            if (!nodeId) {
                m_geometry.removeMesh(meshId);      // no node ever claimed it
                std::cerr << "[warn] Node budget exhausted" << std::endl;
                break;
            }
            m_history.created(m_scene.getNode(nodeId).guid());
            select(nodeId);
            break;
        }

        case EditorCommand::Kind::DuplicateNode:
        {
            const uint32_t sourceId = m_scene.findNode(cmd.node);
            if (!sourceId) {
                break;
            }
            // Snapshot first
            // The snapshot carries every reflected field

            const Node         &src      = m_scene.getNode(sourceId);
            const uint32_t      parentId = src.parentId;
            const uint32_t      meshId   = src.meshId;
            const std::string   label    = nodeLabel(src);
            const reflect::Blob snapshot = reflect::toBlob(src);

            ScopedTransaction tx(m_history, m_editor, "Duplicate " + label);

            // Shares the mesh handle
            const uint32_t nodeId = m_scene.createNode(parentId, {}, meshId);
            if (!nodeId) {
                std::cerr << "[warn] Node budget exhausted" << std::endl;
                break;
            }
            Node &copy = m_scene.getNode(nodeId);
            if (!reflect::readBinary(copy, snapshot)) {
                fatalError("DuplicateNode: node snapshot did not read back");
            }
            copy.name = copy.name.empty() ? "Copy" : copy.name + " Copy";

            m_history.created(copy.guid());
            select(nodeId);
            break;
        }

        case EditorCommand::Kind::DeleteNode:
        {
            const uint32_t nodeId = m_scene.findNode(cmd.node);
            if (!nodeId) {
                break;
            }
            ScopedTransaction tx(m_history, m_editor, "Delete " + nodeLabel(m_scene.getNode(nodeId)));
            (void)m_history.destroy(cmd.node);
            break;
        }

        case EditorCommand::Kind::ReparentNode:
        {
            const uint32_t nodeId   = m_scene.findNode(cmd.node);
            const uint32_t parentId = m_scene.findNode(cmd.parent);
            // A parent that vanished is not the same as "to the root".
            if (!nodeId || (!cmd.parent.isNull() && !parentId)) {
                break;
            }

            ScopedTransaction tx(m_history, m_editor, "Reparent " + nodeLabel(m_scene.getNode(nodeId)));

            const NodePlacement from = m_scene.placementOf(nodeId);
            if (!m_history.touch(EditTarget::forNode(cmd.node))) {
                break;
            }
            if (m_scene.reparentNode(nodeId, parentId)) {
                m_history.moved(cmd.node, from, m_scene.placementOf(nodeId));
                m_structurallyChanged.push_back(cmd.node);
            }
            break;
        }

        //assets and files
        case EditorCommand::Kind::LoadModel:
        {
            m_pendingAssets.push_back({ PendingAsset::Kind::Model, cmd.path });
            break;
        }

        case EditorCommand::Kind::LoadMaterial:
        {
            m_pendingAssets.push_back({ PendingAsset::Kind::Material, cmd.path });
            break;
        }

        case EditorCommand::Kind::CreateMaterial:
        {
            Material mat;
            mat.metallicFactor  = 0.0f;
            mat.roughnessFactor = 0.5f;
            mat.name = cmd.path.empty() ? "New Material" : cmd.path.stem().string();

            const uint32_t materialId = m_resources.addMaterial(mat);
            if (!materialId) {
                break;      // addMaterial already reported the budget
            }

            // Written straight to disk when it was created from the Assets
            // tab, so the file the user just made appears in the folder they
            // made it in. Created from the Materials tab it stays in memory
            // and the inspector offers Save As.
            if (!cmd.path.empty()) {
                const std::filesystem::path path = uniquePath(cmd.path);
                if (saveMaterial(m_resources, m_cache, materialId, path)) {
                    m_resources.setMaterialSource(materialId, path);
                    m_world.markMaterialSaved(materialId);
                }
            }

            m_editor.selectMaterial(materialId);
            break;
        }

        case EditorCommand::Kind::RenameAsset:
        {
            if (cmd.path.empty() || cmd.name.empty()) {
                break;
            }

            std::filesystem::path target = cmd.path.parent_path() / cmd.name;

            if (!target.has_extension() && cmd.path.has_extension()) {
                target.replace_extension(cmd.path.extension());
            }
            if (target == cmd.path) {
                break;
            }
            target = uniquePath(target);

            std::error_code ec;
            std::filesystem::rename(cmd.path, target, ec);
            if (ec) {
                showError("Failed to rename " + cmd.path.filename().string() + ": " + ec.message());
                break;
            }

            // Any material loaded from that file is now pointing at a path
            // that no longer exists, and its next Save would recreate the old
            // name. clearDirty is false: the path moved, the contents did not,
            // so an unsaved edit is still unsaved.
            for (uint32_t i = 1; i <= m_resources.materialCount(); ++i) {
                if (m_resources.materialInfo(i).sourcePath == cmd.path) {
                    m_resources.setMaterialSource(i, target, false);
                }
            }

            // Same for the open scene: Save must follow the file.
            if (!m_scenePath.empty() && m_scenePath == cmd.path) {
                m_scenePath = target;
            }

            // NOTE: renaming an image is not handled here. TextureCache keys on
            // the path, so every .mat referencing the old name silently falls
            // back to the error texture on next load
            break;
        }
        case EditorCommand::Kind::SaveScene:
        {
            // EditorUI always names the file (Save As when untitled). Never
            // mid-gesture: markSaved() requires a closed transaction.
            if (cmd.path.empty() || m_history.isOpen()) {
                sceneSaveFailed = true;
                break;
            }
            std::filesystem::path path = cmd.path;
            if (path.extension() != ".scene") {
                path += ".scene";
            }

            const SceneSaveResult saved = saveScene(m_scene, m_geometry, m_resources, m_cache, path);
            for (const std::string &w : saved.warnings) {
                std::cerr << "[scene warn] " << w << std::endl;
            }
            if (!saved.ok) {
                sceneSaveFailed = true;
                showError("Saving the scene failed:\n" +
                          (saved.warnings.empty() ? std::string("unknown error") : saved.warnings.back()));
                break;
            }

            m_scenePath = path;
            m_history.markSaved();
            std::cout << "[scene] Saved to " << path << std::endl;

            if (!saved.warnings.empty()) {
                std::string message = "Scene saved, but some references could not be written:";
                for (const std::string &w : saved.warnings) {
                    message += "\n  " + w;
                }
                showError(message);
            }
            break;
        }

        case EditorCommand::Kind::NewScene:
        {
            if (sceneSaveFailed || m_history.isOpen()) {
                break;
            }
            clearScene();
            m_scenePath.clear();
            m_history.markSaved();
            std::cout << "[scene] New scene" << std::endl;
            break;
        }

        case EditorCommand::Kind::LoadScene:
        {
            if (sceneSaveFailed || m_history.isOpen()) {
                break;
            }
            if (cmd.path.empty() || !std::filesystem::exists(cmd.path)) {
                showError("No scene file found at " + cmd.path.string());
                break;
            }

            // The file is parsed before clearScene runs, so a broken file
            // leaves the current scene (and its history) alone.
            const SceneLoadResult loaded = loadScene(
                m_scene, m_geometry, m_resources, m_ctx, m_cache, cmd.path,
                [this] { clearScene(); });

            for (const uint32_t materialId : loaded.loadedMaterials) {
                m_world.markMaterialSaved(materialId);
            }
            if (!m_geometry.flushUploads()) {
                showError("Scene load: failed to upload mesh geometry");
            }
            if (!m_resources.commitTextureDescriptors()) {
                showError("Scene load: failed to commit texture descriptors");
            }
            for (const std::string &w : loaded.warnings) {
                std::cerr << "[scene warn] " << w << std::endl;
            }

            if (!loaded.ok) {
                std::string message = "Could not open " + cmd.path.string();
                for (const std::string &w : loaded.warnings) {
                    message += "\n  " + w;
                }
                showError(message);
                break;
            }
            break;
        }

        case EditorCommand::Kind::CreateDirectory:
        {
            std::error_code ec;
            std::filesystem::create_directory(uniquePath(cmd.path), ec);
            if (ec) {
                showError("Failed to create folder: " + ec.message());
            }

            m_scenePath = cmd.path;
            m_history.markSaved();
            std::cout << "[scene] Loaded " << cmd.path << std::endl;
            break;
        }

        case EditorCommand::Kind::AssignTexture:
        {
            if (!cmd.materialId || cmd.materialId > m_resources.materialCount()) {
                break;
            }

            PendingAsset pending;
            pending.kind       = PendingAsset::Kind::Texture;
            pending.path       = cmd.path;
            pending.materialId = cmd.materialId;
            pending.textureId  = cmd.textureId;
            pending.slot       = cmd.textureSlot;
            m_pendingAssets.push_back(std::move(pending));
            break;
        }

        case EditorCommand::Kind::SaveMaterial:
        {
            if (!cmd.materialId || cmd.materialId > m_resources.materialCount()) {
                break;
            }
            std::filesystem::path path = cmd.path;
            if (path.empty()) {
                path = m_resources.materialInfo(cmd.materialId).sourcePath;
            }
            if (path.empty()) {
                std::string name = m_resources.material(cmd.materialId).name;
                if (name.empty()) {
                    name = "Material " + std::to_string(cmd.materialId);
                }
                path = std::filesystem::path(ASSET_DIR) / "materials" / (name + ".mat");
            }

            // Read-only, so unlike a load this needs no deferral.
            if (saveMaterial(m_resources, m_cache, cmd.materialId, path)) {
                m_resources.setMaterialSource(cmd.materialId, path);
                m_world.markMaterialSaved(cmd.materialId);
            }
            break;
        }

        case EditorCommand::Kind::AssignMaterial:
        {
            const uint32_t nodeId = m_scene.findNode(cmd.node);
            if (!nodeId) {
                break;
            }
            const uint32_t meshId = m_scene.getNode(nodeId).meshId;
            if (!m_geometry.meshAlive(meshId)) {
                break;
            }

            ScopedTransaction tx(m_history, m_editor, "Assign Material");
            if (!m_history.touch(EditTarget::forMeshMaterials(meshId))) {
                break;
            }

            // materialId lives on the SubMesh and the renderer reads it fresh
            // every frame into RenderItem::materialIndex

            Mesh &mesh = m_geometry.meshMutable(meshId);
            if (cmd.subMesh == EditorCommand::kAllSubMeshes) {
                for (SubMesh &subMesh : mesh.subMeshes) {
                    subMesh.materialId = cmd.materialId;
                }
            } else if (cmd.subMesh < mesh.subMeshes.size()) {
                mesh.subMeshes[cmd.subMesh].materialId = cmd.materialId;
            }
            break;
        }
        }
    }

    // One submit for every primitive created this frame.
    if (geometryAdded && !m_geometry.flushUploads()) {
        showError("Failed to upload generated geometry");
    }
    m_editor.clearCommands();
}

void Application::clearScene()
{
    m_history.clear();
    m_editor.clearSelection();

    // Destroying the roots frees every node; the returned meshes are the ones
    // no live node references any more.
    std::vector<uint32_t> orphans;
    for (const uint32_t root : m_scene.rootNodes()) {
        m_scene.destroyNode(root, orphans);
    }
    for (const uint32_t meshId : orphans) {
        m_geometry.removeMesh(meshId);
    }

    // Meshes of deleted nodes were kept alive for undo. With the history and
    // the scene both empty nothing can need them
    m_world.collectGarbage(m_history);
    m_world.resetOrphanedMeshes();
}

void Application::updateWindowTitle()
{
    std::string title = m_scenePath.empty() ? std::string("Untitled") : m_scenePath.stem().string();
    if (m_history.isDirty()) {
        title += "*";
    }
    title += " - Learning Vulkan";

    if (title != m_windowTitle) {
        m_windowTitle = std::move(title);
        SDL_SetWindowTitle(m_window, m_windowTitle.c_str());
    }
}

void Application::applyHistoryStep(const UndoHistory::Outcome &outcome, const char *verb)
{
    switch (outcome.result) {
    case UndoHistory::Result::Nothing:
        break;
    case UndoHistory::Result::Done:
        m_editor.setSelection(outcome.selection);
        break;
    case UndoHistory::Result::Failed:
        // The history rolled the step back and discarded itself
        m_editor.setSelection(outcome.selection);
        showError(std::string(verb) + " \"" + outcome.name +
                  "\" failed: the scene no longer matched the undo history. "
                  "The history has been cleared; the scene is unchanged.");
        break;
    }
}

void Application::loadPendingAssets()
{
    if (m_pendingAssets.empty()) {
        return;
    }

    // Load is an undo step of its own
    // must not land inside a gesture that is still open (example: drag spanning frames)
    // It waits for the next frame
    if (m_history.isOpen()) {
        return;
    }

    // Safe mid-frame. Image uploads go through the uploader's own timeline and
    // are ordered ahead of any frame submitted afterwards on the same queue,
    // and commitTextureDescriptors() only ever writes slots past the last
    // committed one -- never a slot an in-flight frame could be sampling.
    //
    // Materials share ONE upload submission for the whole batch, the way
    // GltfLoader::uploadImages does for the images of a single file. The
    // command buffer is opened lazily so a frame that queued only models never
    // opens an uploader slot at all.
    VkCommandBuffer materialUploads = nullptr;
    bool texturesAdded = false;

    for (const PendingAsset &asset : m_pendingAssets) {
        if (asset.kind == PendingAsset::Kind::Model) {
            // New roots are appended to the root chain, so everything past
            // the old count is this file's. Recorded even if the load fails
            const size_t rootsBefore = m_scene.rootNodes().size();
            ScopedTransaction tx(m_history, m_editor, "Import " + asset.path.filename().string());

            loadData(asset.path);   // reports its own failures; a bad drop is not fatal

            const std::vector<uint32_t> roots = m_scene.rootNodes();
            for (size_t i = rootsBefore; i < roots.size(); ++i) {
                m_history.created(m_scene.getNode(roots[i]).guid());
            }
            continue;
        }

        if (asset.kind == PendingAsset::Kind::Texture) {
            uint32_t textureId = asset.textureId;
            if (textureId > m_resources.textureCount()) {
                textureId = 0;
            }

            if (!textureId && !asset.path.empty()) {
                // Outside ASSET_DIR there is no portable way to record the
                // reference, so the material could never be saved with it.
                // Refuse instead of producing an unsaveable material.
                const std::string relative = m_cache.toRelative(asset.path);
                if (relative.empty()) {
                    showError("Textures must live under the asset folder: " + asset.path.string());
                    continue;
                }

                if (!materialUploads) {
                    materialUploads = m_ctx.beginUpload();
                    if (!materialUploads) {
                        showError("Failed to open an upload command buffer for textures");
                        break;
                    }
                }

                textureId = m_cache.acquireTexture(m_ctx, m_resources, materialUploads,
                                                   relative, slotIsSrgb(asset.slot));
                if (!textureId) {
                    continue;   // acquireTexture reported it
                }
                texturesAdded = true;
            }

            ScopedTransaction tx(m_history, m_editor, "Assign Texture");
            const EditTarget target = EditTarget::forMaterial(asset.materialId);
            if (!m_history.touch(target)) {
                continue;       // material ID out of range
            }

            Material mat = m_resources.material(asset.materialId);
            switch (asset.slot) {
            case TextureSlot::BaseColor:         mat.baseColorTexture         = textureId; break;
            case TextureSlot::MetallicRoughness: mat.metallicRoughnessTexture = textureId; break;
            case TextureSlot::Normal:            mat.normalTexture            = textureId; break;
            case TextureSlot::Occlusion:         mat.occlusionTexture         = textureId; break;
            case TextureSlot::Emissive:          mat.emissiveTexture          = textureId; break;
            }
            if (!m_world.apply(target, reflect::toBlob(mat))) {
                showError("Failed to assign the texture");
            }
            continue;
        }

        if (!materialUploads) {
            materialUploads = m_ctx.beginUpload();
            if (!materialUploads) {
                showError("Failed to open an upload command buffer for materials");
                break;
            }
        }

        const uint32_t materialId =
            loadMaterial(m_ctx, m_resources, m_cache, materialUploads, asset.path);
        if (materialId) {
            m_resources.setMaterialSource(materialId, asset.path);
            m_world.markMaterialSaved(materialId);
            texturesAdded = true;
        }
    }

    if (materialUploads) {
        m_ctx.submitUpload();
    }

    // Only new texture slots need committing, and only past the last committed
    // one -- so this cannot disturb a frame already in flight.
    if (texturesAdded && !m_resources.commitTextureDescriptors()) {
        showError("Failed to commit texture descriptors after loading materials");
    }

    m_pendingAssets.clear();
}

void Application::pickAt(float mouseX, float mouseY)
{
    // camera's aspect ratio is
    // built from the window size, and SDL reports mouse positions in the same
    // space
    const Ray ray = screenPointToRay(m_camera, mouseX, mouseY, m_width, m_height);

    PickResult hit = pickNode(m_scene, m_geometry, ray);

    // Lights win ties. Their handle is small and usually sits in front of
    // whatever it is lighting, so "nearest wins" on its own would make a light
    // standing on the ground plane nearly unclickable.
    const PickResult light = pickLight(m_scene, ray);
    if (light && light.distance <= hit.distance) {
        hit = light;
    }

    // A miss selects the null Guid
    m_editor.selectNode(hit ? m_scene.getNode(hit.nodeId).guid() : Guid{}, hit.subMesh);
}

void Application::shutdown()
{
    std::cout << "Cleaning up and shutting down" << std::endl;

    // Nothing may be destroyed while the GPU could still be reading it.
    if (m_ctx.device()) {
        vkDeviceWaitIdle(m_ctx.device());
    }

    // Reverse of initialize(). Each subsystem destroys only what it owns, so
    // there is exactly one destroy call per handle -- the old shutdown()
    // freed every frame semaphore and command pool twice.
    m_editor.shutdown(m_ctx);
    m_renderer.shutdown();
    m_geometry.shutdown();
    m_resources.shutdown();
    m_swapchain.destroy();
    m_ctx.shutdown();

    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
}