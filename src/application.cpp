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

    return true;
}

bool Application::loadData(const std::filesystem::path &modelPath)
{
    GltfLoader loader(m_ctx, m_resources, m_geometry, m_scene);
    if (!loader.load(modelPath)) {
        showError("Failed to load model: " + modelPath.string());
        return false;
    }

    // Scale the root down; the test model is authored huge.
    // (Still following the tutorial here -- this belongs in scene setup later.)
    if (const uint32_t rootId = m_scene.rootNodeId()) {
        Node &root = m_scene.getNode(rootId);
        root.setScale(glm::vec3(0.01f));
        root.setTranslation(glm::vec3(0.0f, -5.0f, 0.0f));
    }

    // Everything below has to happen after ALL loading is done, because each
    // of these commits a snapshot of a store to the GPU. If you later add a
    // second loadData() call you need to re-run all three, not just the first.
    if (!m_geometry.uploadToGpu()) {
        showError("Failed to upload geometry to device memory");
        return false;
    }

    if (!m_resources.updateTextureDescriptors()) {
        showError("Failed to write the bindless texture descriptors");
        return false;
    }

    if (!m_resources.uploadMaterialBuffer()) {
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
            m_camera.handleInput(event, deltaTime);

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


        m_renderer.render(m_scene, m_camera, m_width, m_height);
    }
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