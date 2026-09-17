#include "ShaderCompiler.h"

#include <volk.h>
#include <shaderc/shaderc.hpp>
#include <fstream>
#include <iostream>
#include <sstream>

#include "../../common/errors.h"

namespace {
    std::string readTextFile(const std::string &filePath)
    {
        std::ifstream infile(filePath);
        if (!infile.is_open()) {
            return {};
        }
        std::stringstream buff;
        buff << infile.rdbuf();
        return buff.str();
    }
}

VkShaderModule compileShaderModule(VkDevice device, const std::string &fileName,
                                   shaderc_shader_kind kind, std::string *error)
{
    const std::string shaderPath = SHADER_DIR + fileName;
    const std::string src = readTextFile(shaderPath);
    if (src.empty()) {
        const std::string msg = "Shader file does not exist or is empty: " + shaderPath;
        if (error) { *error = msg; } else { showError(msg); }
        return nullptr;
    }

    std::cout << "Compiling shader: " << shaderPath << std::endl;

    shaderc::Compiler compiler;
    shaderc::CompileOptions opts;
    opts.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_4);
    opts.SetTargetSpirv(shaderc_spirv_version_1_6);
    opts.SetOptimizationLevel(shaderc_optimization_level_performance);

    const shaderc::SpvCompilationResult result =
        compiler.CompileGlslToSpv(src, kind, fileName.c_str(), opts);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        if (error) {
            *error = result.GetErrorMessage();
        } else {
            std::cerr << "Shader compilation error: " << result.GetErrorMessage() << std::endl;
        }
        return nullptr;
    }

    const size_t shaderSize = (result.cend() - result.cbegin()) * sizeof(uint32_t);

    VkShaderModuleCreateInfo shaderModuleCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shaderSize,
        .pCode = result.cbegin()
    };

    VkShaderModule shaderModule = nullptr;
    if (vkCreateShaderModule(device, &shaderModuleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        const std::string msg = "Failed to create shader module for " + fileName;
        if (error) { *error = msg; } else { showError(msg); }
        return nullptr;
    }
    return shaderModule;
}
