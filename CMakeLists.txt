cmake_minimum_required(VERSION 3.24)
project(Vulkan)

find_package(SDL3 REQUIRED)
find_package(Vulkan REQUIRED COMPONENTS volk shaderc_combined)
find_package(glm REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SHADERC REQUIRED IMPORTED_TARGET shaderc)

add_executable(vulkanapp
        src/main.cpp
        src/application.h
        src/application.cpp
        src/utils.h
        src/external/tinygltf.cpp
        src/structs.h
        src/Math/Transform.h
        src/common/constants.h
        src/common/constants.h
        src/Math/Transform.h
        src/Math/Transform.cpp
        src/Math/LocalFrame.h
)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# For MSVC, you often need the 'latest' flag specifically


target_link_libraries(vulkanapp PRIVATE
        Vulkan::Vulkan
        Vulkan::volk
        PkgConfig::SHADERC
        SDL3::SDL3
        glm::glm)

target_compile_definitions(vulkanapp PRIVATE
        SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/")

target_include_directories(vulkanapp PRIVATE /src/external)