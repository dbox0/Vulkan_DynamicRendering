cmake_minimum_required(VERSION 3.24)
project(Vulkan)

include(FetchContent)

find_package(SDL3 REQUIRED)
find_package(Vulkan REQUIRED COMPONENTS volk shaderc_combined)
find_package(glm REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SHADERC REQUIRED IMPORTED_TARGET shaderc)

FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.9-docking)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.12.0)
FetchContent_MakeAvailable(json)

add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3 Vulkan::Headers)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK VK_NO_PROTOTYPES)

FetchContent_Declare(imguizmo
        GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
        GIT_TAG        master)
FetchContent_MakeAvailable(imguizmo)

# ImGuizmo's own CMakeLists only defines an imgui target when it is the top
# level project. Under FetchContent it is not, so it would compile with no
# imgui include path at all unless we hand it ours. imgui_internal.h comes
# along with it -- ImGuizmo.cpp includes that directly.
target_link_libraries(imguizmo PUBLIC imgui)

# Set BEFORE add_executable: CXX_STANDARD is copied onto a target when the
# target is created, so the old placement below the target did nothing and
# vulkanapp was building at the compiler default.
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(vulkanapp
        src/main.cpp
        src/application.h
        src/application.cpp
        src/utils.h
        src/external/tinygltf.cpp
        src/Math/Transform.h
        src/common/constants.h
        src/common/constants.h
        src/Math/Transform.h
        src/Math/Transform.cpp
        src/Math/LocalFrame.h
        src/scene/Geometry/Node.h
        src/scene/Geometry/NodeWorld.h
        src/common/errors.cpp
        src/common/errors.h
        src/common/gpu_types.h
        src/scene/Scene.cpp
        src/scene/Scene.h
        src/editor/Picking.cpp
        src/editor/Picking.h
        src/editor/EditorUI.cpp
        src/editor/EditorUI.h
        src/editor/EditorInspectorPanels.cpp
        src/editor/EditorInteraction.cpp
        src/editor/EditorScenePanels.cpp
        src/editor/EditorCommands.h
        src/assets/PrimitiveBuilder.cpp
        src/assets/PrimitiveBuilder.h
        src/scene/Camera.cpp
        src/scene/Camera.h
        src/assets/GltfLoader.cpp
        src/assets/GltfLoader.h
        src/render/VulkanContext.cpp
        src/render/VulkanContext.h
        src/render/Renderer.cpp
        src/render/Renderer.h
        src/render/Swapchain.cpp
        src/render/Swapchain.h
        src/render/ShadowMap.cpp
        src/render/ShadowMap.h
        src/render/ResourceStore.cpp
        src/render/ResourceStore.h
        src/render/GeometryStore.cpp
        src/render/GeometryStore.h
        src/engine/Component.cpp
        src/engine/Component.h
        src/engine/Entity.cpp
        src/engine/Entity.h
        src/engine/components/TransformComponent.h
        src/engine/components/MeshComponent.cpp
        src/engine/components/MeshComponent.h
        src/render/GpuShared.h
        src/assets/Material.h
        src/assets/Mesh.h
        src/render/Uploader.h
        src/render/Uploader.cpp
        src/render/RangeAllocator.h
        src/assets/TextureCache.h
        src/assets/TextureCache.cpp
        src/assets/MaterialSerializer.h
        src/assets/MaterialSerializer.cpp
        src/common/Fatal.h
        src/common/Guid.h
        src/common/Guid.cpp
        src/reflect/Reflection.h
        src/reflect/Reflection.cpp
        src/reflect/BinaryArchive.h
        src/reflect/BinaryArchive.cpp
        src/reflect/JsonArchive.h
        src/reflect/JsonArchive.cpp
        src/scene/SceneTypes.h
        src/scene/SceneTypes.cpp
        src/assets/AssetTypes.h
        src/assets/AssetTypes.cpp
        src/editor/EditTarget.h
        src/editor/EditRecorder.h
        src/editor/EditRecorder.cpp
        src/editor/EditorSelection.h
        src/editor/UndoHistory.h
        src/editor/UndoHistory.cpp
        src/editor/MutationDetector.h
        src/editor/MutationDetector.cpp
        src/scene/SceneSerializer.h
        src/scene/SceneSerializer.cpp
        src/editor/SceneUndoWorld.h
        src/editor/SceneUndoWorld.cpp
        src/editor/EditorWorld.h
        src/editor/EditorWorld.cpp
        src/scene/SceneSnapshot.h
        src/scene/SceneDrawItems.cpp
)

# For MSVC, you often need the 'latest' flag specifically


target_link_libraries(vulkanapp PRIVATE
        Vulkan::Vulkan
        Vulkan::volk
        PkgConfig::SHADERC
        SDL3::SDL3
        glm::glm
        imgui
        imguizmo
        nlohmann_json::nlohmann_json)

target_compile_definitions(vulkanapp PRIVATE
        SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/"
        ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/res/")

target_include_directories(vulkanapp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/external)

# ---------------------------------------------------------------------------
# Tests
#
# Only glm and nlohmann: no GPU, SDL or Vulkan SDK involved, so this runs in
# CI as-is. Build and run with:  cmake --build . --target engine_tests
#                                ctest --output-on-failure
# ---------------------------------------------------------------------------
enable_testing()

add_executable(engine_tests
        tests/main.cpp
        tests/TestFramework.h
        tests/reflection_tests.cpp
        tests/edit_recorder_tests.cpp
        tests/undo_tests.cpp
        tests/serializer_tests.cpp
        src/common/Guid.cpp
        src/reflect/Reflection.cpp
        src/reflect/BinaryArchive.cpp
        src/reflect/JsonArchive.cpp
        src/scene/SceneTypes.cpp
        src/assets/AssetTypes.cpp
        src/editor/EditRecorder.cpp
        src/editor/UndoHistory.cpp
        src/editor/SceneUndoWorld.cpp
        src/scene/Scene.cpp)

target_link_libraries(engine_tests PRIVATE
        glm::glm
        nlohmann_json::nlohmann_json)

add_test(NAME engine COMMAND engine_tests)
