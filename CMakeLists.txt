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
        GIT_TAG        v1.92.0-docking)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
        src/editor/EditorUI.cpp
        src/editor/EditorUI.h
        src/editor/EditorInspectorPanels.cpp
        src/editor/Picking.cpp
        src/editor/Picking.h
)

target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3 Vulkan::Headers)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK VK_NO_PROTOTYPES)

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
        src/render/GpuShared.h
        src/assets/Material.h
        src/assets/Mesh.h
)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# For MSVC, you often need the 'latest' flag specifically


target_link_libraries(vulkanapp PRIVATE
        Vulkan::Vulkan
        Vulkan::volk
        PkgConfig::SHADERC
        SDL3::SDL3
        glm::glm
        imgui)

target_compile_definitions(vulkanapp PRIVATE
        SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/"
        ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/res/")

target_include_directories(vulkanapp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/external)