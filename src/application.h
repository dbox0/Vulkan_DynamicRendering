#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include "assets/PrimitiveBuilder.h"
#include "assets/TextureCache.h"
#include "editor/EditorCommands.h"
#include "render/VulkanContext.h"
#include "render/Swapchain.h"
#include "render/ResourceStore.h"
#include "render/GeometryStore.h"
#include "render/Renderer.h"
#include "scene/Scene.h"
#include "scene/Camera.h"
#include "editor/EditorUI.h"
#include "editor/EditRecorder.h"
#include "editor/EditorWorld.h"
#include "editor/MutationDetector.h"
#include "scene/SceneSerializer.h"
#include "editor/UndoHistory.h"
#include "editor/Picking.h"

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

    // Left-click in the viewport -> ray cast -> inspector selection.
    void pickAt(float mouseX, float mouseY);

    // Applies everything EditorUI queued during build(). Runs between build()
    // and render(), so a node created this frame is drawn this frame.
    void applyEditorCommands();

    // Assets queued by a window file-drop or a Project panel double-click, so
    // the load happens between build() and render() rather than mid-event.
    void loadPendingAssets();

    // Empties the scene, the undo history and the editor selection. Shared by
    // New Scene and Load Scene.
    void clearScene();

    // "<scene>[*] - Learning Vulkan", only touched when it changes.
    void updateWindowTitle();

    // Selection and error reporting after an undo or redo.
    void applyHistoryStep(const UndoHistory::Outcome &outcome, const char *verb);

    struct PendingAsset
    {
        enum class Kind : uint8_t { Model, Material, Texture };

        Kind                  kind = Kind::Model;
        std::filesystem::path path;

        // Texture only. Either textureId (already resident) or path (a file
        // to load); neither set is a clear.
        uint32_t    materialId = 0;
        uint32_t    textureId  = 0;
        TextureSlot slot       = TextureSlot::BaseColor;
    };

    // Validates the editor's command stream across frames (edit runs are
    // well formed, nothing interleaves them) -- the transaction boundaries
    // below are read straight from it.
    EditStreamChecker         m_editChecker;
    std::vector<Guid>         m_structurallyChanged;   // scratch, see applyEditorCommands
    std::vector<PendingAsset> m_pendingAssets;

    // The open scene's file; empty while untitled. EditorUI reads it to decide whether Save needs a name.
    std::filesystem::path     m_scenePath;
    std::string               m_windowTitle;

    static constexpr uint32_t VulkanVersion     = VK_API_VERSION_1_4;
    static constexpr size_t   MaxNodes          = 1024;
    static constexpr size_t VertexBudgetBytes = 128ull * 1024 * 1024;
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

    // Holds no Vulkan handles of its own -- only IDs into m_resources -- so it
    // needs nothing in shutdown().
    TextureCache  m_cache;
    Renderer      m_renderer;
    EditorUI m_editor;

    // Last, so they are destroyed first: both hold references to the stores
    // above
    EditorWorld   m_world{ m_scene, m_resources, m_geometry };
    UndoHistory       m_history{ m_world };
    MutationDetector  m_detector;
};