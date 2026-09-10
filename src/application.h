#pragma once
#include <cstdint>
#include <filesystem>
#include "render/VulkanContext.h"
#include "render/Swapchain.h"
#include "render/ResourceStore.h"
#include "render/GeometryStore.h"
#include "render/Renderer.h"
#include "scene/Scene.h"
#include "scene/Camera.h"

struct SDL_Window;

// Window, main loop, and wiring. Nothing else.
class Application
{
public:
    Application();
    Application(const Application &) = delete;
    Application &operator=(const Application &) = delete;

    bool initialize();
    bool loadData(const std::filesystem::path &modelPath);
    void run();
    void shutdown();

private:
    bool initializeWindow();

    static constexpr uint32_t VulkanVersion     = VK_API_VERSION_1_4;
    static constexpr size_t   MaxNodes          = 1024;
    static constexpr size_t   VertexBudgetBytes = 64ull * 1024 * 1024;
    static constexpr size_t   IndexBudgetBytes  = 32ull * 1024 * 1024;
    static constexpr uint32_t MaxDrawsPerFrame  = 8192;

    SDL_Window *m_window = nullptr;
    uint32_t m_width  = 1280;
    uint32_t m_height = 720;
    bool m_running = false;

    // Declaration order IS destruction order (reversed). The context must be
    // declared first so it outlives everything holding a reference to it.
    // shutdown() still tears down explicitly, because Vulkan destruction
    // order matters more than C++ can express here.
    VulkanContext m_ctx;
    Swapchain     m_swapchain;
    ResourceStore m_resources;
    GeometryStore m_geometry;
    Scene         m_scene;
    Camera        m_camera;
    Renderer      m_renderer;
};