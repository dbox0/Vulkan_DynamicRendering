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

FetchContent_Declare(meshoptimizer
        GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
        GIT_TAG        v1.2)
FetchContent_MakeAvailable(meshoptimizer)

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


set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(vulkanapp
        src/app/main.cpp
        src/app/application.h
        src/app/application.cpp
        third_party/tinygltf.cpp
        src/common/constants.h
        src/scene/geometry/Node.h
        src/scene/geometry/NodeWorld.h
        src/common/errors.cpp
        src/common/errors.h
        src/render/core/gpu_types.h
        src/scene/Scene.cpp
        src/scene/Scene.h
        src/editor/Picking.cpp
        src/editor/Picking.h
        src/editor/EditorUI.cpp
        src/editor/EditorUI.h
        src/editor/EditorInspectorPanels.cpp
        src/editor/EditorProjectPanels.cpp
        src/editor/EditorRenderPanels.cpp
        src/editor/EditorTheme.cpp
        src/editor/EditorTheme.h
        src/editor/EditorWidgets.cpp
        src/editor/EditorWidgets.h
        src/editor/EditorInteraction.cpp
        src/editor/EditorScenePanels.cpp
        src/editor/EditorCommands.h
        src/assets/PrimitiveBuilder.cpp
        src/assets/PrimitiveBuilder.h
        src/scene/Camera.cpp
        src/scene/Camera.h
        src/assets/GltfLoader.cpp
        src/assets/GltfLoader.h
        src/render/Renderer.cpp
        src/render/Renderer.h
        src/render/GpuShared.h
        src/render/core/VulkanContext.cpp
        src/render/core/VulkanContext.h
        src/render/core/Swapchain.cpp
        src/render/core/Swapchain.h
        src/render/core/Uploader.cpp
        src/render/core/Uploader.h
        src/render/resources/ResourceStore.cpp
        src/render/resources/ResourceStore.h
        src/render/resources/GeometryStore.cpp
        src/render/resources/GeometryStore.h
        src/render/resources/RangeAllocator.h
        src/render/shaders/ShaderCompiler.cpp
        src/render/shaders/ShaderCompiler.h
        src/render/shaders/ShaderProgram.cpp
        src/render/shaders/ShaderProgram.h
        src/render/shaders/ShaderWatcher.cpp
        src/render/shaders/ShaderWatcher.h
        src/render/passes/ScenePass.cpp
        src/render/passes/ScenePass.h
        src/render/shadows/ShadowMap.cpp
        src/render/shadows/ShadowMap.h
        src/render/shadows/ShadowPass.cpp
        src/render/shadows/ShadowPass.h
        src/render/passes/SelectionOutlinePass.cpp
        src/render/passes/SelectionOutlinePass.h
        src/render/passes/DebugLinePass.cpp
        src/render/passes/DebugLinePass.h
        src/assets/Material.h
        src/assets/Mesh.h
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
        src/render/post/TonemapPass.cpp
        src/render/post/TonemapPass.h
        src/math/Plane.h
        src/render/ibl/EnvironmentMap.cpp
        src/render/ibl/EnvironmentMap.h
        src/render/ibl/HdrLoader.cpp
        src/render/ibl/HdrLoader.h
        src/render/passes/SkyboxPass.cpp
        src/render/passes/SkyboxPass.h
        src/render/core/vkbarrier.cpp
        src/render/core/vkbarrier.h
        src/render/post/BloomPass.cpp
        src/render/post/BloomPass.h
        src/render/core/GPUProfiler.cpp
        src/render/core/GPUProfiler.h
        src/math/Frustum.h
        src/math/Frustum.cpp
        src/render/culling/CullSettings.h
        src/assets/ImportMesh.h
        src/assets/MeshProcessing.cpp
        src/assets/VertexPacking.h
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
        nlohmann_json::nlohmann_json
        meshoptimizer
)

target_compile_definitions(vulkanapp PRIVATE
        SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders/"
        ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/res/")

target_include_directories(vulkanapp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

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
        src/scene/Scene.cpp
        src/assets/ImportMesh.h
        src/assets/MeshProcessing.cpp
)

target_link_libraries(engine_tests PRIVATE
        glm::glm
        nlohmann_json::nlohmann_json
        meshoptimizer
)

add_test(NAME engine COMMAND engine_tests)
