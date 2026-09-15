#include "application.h"

#include <SDL3/SDL.h>
#include <iostream>

#include "assets/GltfLoader.h"
#include "assets/Mesh.h"
#include "assets/MaterialSerializer.h"
#include "assets/PrimitiveBuilder.h"
#include "common/errors.h"
#include "editor/EditorCommands.h"
#include "scene/Geometry/Node.h"

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

    if (!m_ctx.initialize(m_window, VulkanVersion)) {
        showError("Failed to initialize the Vulkan context");
        return false;
    }

    if (!m_swapchain.create(m_width, m_height)) {
        showError("Failed to create the swapchain");
        return false;
    }

    // Must come before the renderer: createPipeline() needs the global
    // descriptor set layout this owns.
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

                // The Project panel is rooted at ASSET_DIR and cannot navigate
                // above it, so dropping a file on the window is the only way to
                // reach a model living anywhere else on disk.
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
                // wantsMouse() already excluded clicks that landed on a panel,
                // so anything arriving here is a click in the viewport.
                // The gizmo needs a check of its own: ImGuizmo draws into a
                // NoInputs window, so ImGui never reports capture for it and
                // the click that grabs an axis handle would also deselect the
                // node. One frame stale, since events are polled before
                // build() runs -- bounded by one frame of mouse movement.
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                    event.button.button == SDL_BUTTON_LEFT &&
                    !m_editor.gizmoCapturesMouse()) {
                    pickAt(event.button.x, event.button.y);
                }
                m_camera.handleInput(event, deltaTime);
            }
        }
        m_camera.Update(deltaTime);
        if (!m_running) {
            break;
        }



        // Minimised window: no valid extent to render into, so idle instead
        // of feeding a 0x0 swapchain.
        if (m_width == 0 || m_height == 0) {
            SDL_Delay(16);
            continue;
        }

        // NewFrame must not run on a frame that gets skipped above -- ImGui
        // asserts if NewFrame is called twice without a Render in between.
        m_editor.beginFrame();
        m_editor.build(m_scene, m_geometry, m_resources, m_camera, m_width, m_height);

        // After build(), before render(): the commands mutate containers the
        // panels were iterating, and the draw list they produced has already
        // copied every string it needs.
        applyEditorCommands();
        loadPendingAssets();

        // Pushed in rather than pulled out: the renderer knows nothing about
        // EditorUI, so a build without an editor still compiles and runs.
        //
        // Moved below the commands -- reading the selection after them means a
        // node created or deleted this frame gets the right outline on that
        // frame rather than the next one.
        m_renderer.setSelection(m_editor.selectedNode());

        m_renderer.render(m_scene, m_camera, m_width, m_height,
                         [this](VkCommandBuffer cmd) { m_editor.record(cmd); });
    }
}

void Application::applyEditorCommands()
{
    const std::vector<EditorCommand> &commands = m_editor.commands();
    if (commands.empty()) {
        return;
    }

    bool geometryAdded = false;

    for (const EditorCommand &cmd : commands) {
        switch (cmd.kind) {

        case EditorCommand::Kind::CreateEmpty:
        {
            if (!m_scene.createNode(cmd.parentId, "Empty")) {
                std::cerr << "[warn] Node budget exhausted" << std::endl;
            }
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

            const uint32_t nodeId = m_scene.createNode(cmd.parentId,
                                                       primitiveName(cmd.primitive), meshId);
            if (!nodeId) {
                m_geometry.removeMesh(meshId);      // no node ever claimed it
                std::cerr << "[warn] Node budget exhausted" << std::endl;
                break;
            }
            m_editor.selectNode(nodeId, 0);
            break;
        }

        case EditorCommand::Kind::DuplicateNode:
        {
            if (!m_scene.isAlive(cmd.nodeId)) {
                break;
            }
            // Copied by value first: createNode() writes a fresh Node into a
            // recycled slot, and a reference into NodeWorld taken before that
            // is a hazard even though the storage itself never reallocates.
            const Node       &src         = m_scene.getNode(cmd.nodeId);
            const uint32_t    parentId    = src.parentId;
            const uint32_t    meshId      = src.meshId;
            const std::string name        = src.name;
            const glm::vec3   translation = src.getTranslation();
            const glm::quat   rotation    = src.getRotation();
            const glm::vec3   scale       = src.getScale();

            // Shares the mesh handle rather than rebuilding it -- two nodes
            // pointing at one mesh is exactly what glTF instancing produces,
            // and Scene::destroyNode already refcounts for it.
            const uint32_t nodeId = m_scene.createNode(parentId,
                                                       name.empty() ? "Copy" : name + " Copy",
                                                       meshId);
            if (!nodeId) {
                std::cerr << "[warn] Node budget exhausted" << std::endl;
                break;
            }
            Node &copy = m_scene.getNode(nodeId);
            copy.setTranslation(translation);
            copy.setRotation(rotation);
            copy.setScale(scale);

            m_editor.selectNode(nodeId, 0);
            break;
        }

        case EditorCommand::Kind::DeleteNode:
        {
            m_orphanedMeshes.clear();
            m_scene.destroyNode(cmd.nodeId, m_orphanedMeshes);

            // Only meshes no surviving node references. removeMesh defers the
            // range free until the frames that could still be reading it have
            // retired, so this is safe to call mid-frame.
            for (const uint32_t meshId : m_orphanedMeshes) {
                m_geometry.removeMesh(meshId);
            }
            break;
        }

        case EditorCommand::Kind::ReparentNode:
        {
            m_scene.reparentNode(cmd.nodeId, cmd.parentId);
            break;
        }

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
            // A fresh Material, not a copy of anything. The struct defaults
            // are the glTF ones -- metallic 1, roughness 1 -- which renders
            // as a dark mirror and reads as "broken" rather than "new". A
            // dielectric at half roughness is what you actually want to start
            // from, so that is what the editor hands you.
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
                }
            }

            m_editor.selectMaterial(materialId);
            break;
        }

        case EditorCommand::Kind::CreateDirectory:
        {
            std::error_code ec;
            std::filesystem::create_directory(uniquePath(cmd.path), ec);
            if (ec) {
                showError("Failed to create folder: " + ec.message());
            }
            break;
        }

        case EditorCommand::Kind::AssignTexture:
        {
            if (!cmd.materialId || cmd.materialId > m_resources.materialCount()) {
                break;
            }

            // Deferred even when it is only a clear, so assignment order is
            // the order the user made them in -- a drop followed by a clear in
            // the same frame must not resolve backwards.
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

            // Explicit path wins; then wherever it was loaded from; then a
            // default under ASSET_DIR/materials. No dialog yet, which is why
            // MaterialInfo::sourcePath exists at all.
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
            }
            break;
        }

        case EditorCommand::Kind::AssignMaterial:
        {
            if (!m_scene.isAlive(cmd.nodeId)) {
                break;
            }
            const uint32_t meshId = m_scene.getNode(cmd.nodeId).meshId;
            if (!m_geometry.meshAlive(meshId)) {
                break;
            }

            // materialId lives on the SubMesh and the renderer reads it fresh
            // every frame into RenderItem::materialIndex -- no upload, no
            // descriptor churn, visible next frame.
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

void Application::loadPendingAssets()
{
    if (m_pendingAssets.empty()) {
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
            loadData(asset.path);   // reports its own failures; a bad drop is not fatal
            continue;
        }

        if (asset.kind == PendingAsset::Kind::Texture) {
            // Dragged out of the Textures tab: already resident, already in
            // whatever colour space loaded it first. No decode, no upload, no
            // descriptor churn -- and if that space is wrong for this slot the
            // inspector's "expected sRGB" warning is what says so, rather than
            // this silently re-decoding a second copy.
            uint32_t textureId = asset.textureId;
            if (textureId > m_resources.textureCount()) {
                textureId = 0;
            }

            if (!textureId && !asset.path.empty()) {
                // Outside ASSET_DIR there is no portable way to record the
                // reference, so the material could never be saved with it.
                // Refuse at the point of assignment rather than silently
                // producing an unsaveable material.
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

            Material mat = m_resources.material(asset.materialId);
            switch (asset.slot) {
            case TextureSlot::BaseColor:         mat.baseColorTexture         = textureId; break;
            case TextureSlot::MetallicRoughness: mat.metallicRoughnessTexture = textureId; break;
            case TextureSlot::Normal:            mat.normalTexture            = textureId; break;
            case TextureSlot::Occlusion:         mat.occlusionTexture         = textureId; break;
            case TextureSlot::Emissive:          mat.emissiveTexture          = textureId; break;
            }
            m_resources.updateMaterial(asset.materialId, mat);
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
    const PickResult hit = pickNode(m_scene, m_geometry, ray);

    // A miss selects node 0, which is how the inspector already spells
    // "nothing selected" -- clicking empty space deselects.
    m_editor.selectNode(hit.nodeId, hit.subMesh);
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