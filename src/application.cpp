#include "application.h"

#include <SDL3/SDL.h>
#include <iostream>

#include "assets/GltfLoader.h"
#include "common/errors.h"
#include "scene/Geometry/Node.h"

Application::Application()

    : m_swapchain(m_ctx)
    , m_resources(m_ctx)
    , m_geometry(m_ctx)
    , m_renderer(m_ctx, m_swapchain, m_resources, m_geometry)
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
    const GeometryStore::BatchMark mark = m_geometry.mark();

    GltfLoader loader(m_ctx, m_resources, m_geometry, m_scene);
    if (!loader.load(modelPath)) {
        showError("Failed to load model: " + modelPath.string());
        return false;
    }

    if (!m_geometry.uploadSince(mark)) {
        showError("Failed to upload geometry to device memory");
        return false;
    }

    if (!m_resources.commitTextureDescriptors()){
        showError("Failed to upload the material buffer");
        return false;
    }

    std::cout << "Loaded " << m_geometry.meshCount() << " meshes, "
              << m_resources.materialCount() << " materials" << std::endl;
    return true;
}

void Application::run()
{


    // Leave this like this for now
    // TODO: REMOVE THIS TRASH
    Node &root = m_scene.getNode(m_scene.rootNodeId());
    root.setScale(glm::vec3(20.0f));
    root.setTranslation(glm::vec3(0,2,0));

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
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                    event.button.button == SDL_BUTTON_LEFT) {
                    pickAt(event.button.x, event.button.y);
                }
                m_camera.handleInput(event, deltaTime);
            }
        }
        m_camera.Update(deltaTime);
        if (!m_running) {
            break;
        }



        // Pushed in rather than pulled out: the renderer knows nothing about
        // EditorUI, so a build without an editor still compiles and runs.
        m_renderer.setSelection(m_editor.selectedNode());
        // Minimised window: no valid extent to render into, so idle instead
        // of feeding a 0x0 swapchain.
        if (m_width == 0 || m_height == 0) {
            SDL_Delay(16);
            continue;
        }

        // NewFrame must not run on a frame that gets skipped above -- ImGui
        // asserts if NewFrame is called twice without a Render in between.
        m_editor.beginFrame();
        m_editor.build(m_scene, m_geometry, m_resources);

        // Swallow camera input while a widget has focus, or WASD types into
        // a text field and dragging a slider spins the view.


        m_renderer.render(m_scene, m_camera, m_width, m_height,
                         [this](VkCommandBuffer cmd) { m_editor.record(cmd); });
    }
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