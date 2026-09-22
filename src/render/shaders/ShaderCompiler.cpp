#include "ShaderCompiler.h"

#include <volk.h>
#include <shaderc/shaderc.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>

#include "../../common/errors.h"

namespace fs = std::filesystem;

namespace {
    // nullopt = could not open; an empty string is a legitimately empty file.
    std::optional<std::string> readTextFile(const fs::path &filePath)
    {
        std::ifstream infile(filePath);
        if (!infile.is_open()) {
            return std::nullopt;
        }
        std::stringstream buff;
        buff << infile.rdbuf();
        return buff.str();
    }

    void addDependency(std::vector<std::string> *deps, const std::string &name)
    {
        if (deps && std::ranges::find(*deps, name) == deps->end()) {
            deps->push_back(name);
        }
    }

    class ShaderIncluder final : public shaderc::CompileOptions::IncluderInterface
    {
    public:
        ShaderIncluder(fs::path root, std::vector<std::string> *deps)
            : m_root(std::move(root)), m_deps(deps) {}

        shaderc_include_result *GetInclude(const char *requested, shaderc_include_type type,
                                           const char *requesting, size_t /*depth*/) override
        {
            std::vector<fs::path> candidates;
            if (type == shaderc_include_type_relative) {
                candidates.push_back(fs::path(requesting).parent_path() / requested);
            }
            candidates.emplace_back(requested);

            for (const fs::path &candidate : candidates) {
                const std::string name = candidate.lexically_normal().generic_string();
                if (name.starts_with("..")) {
                    continue;   // stay inside SHADER_DIR
                }
                if (auto content = readTextFile(m_root / name)) {
                    addDependency(m_deps, name);
                    return makeResult(name, std::move(*content));
                }
            }
            for (const fs::path &candidate : candidates) {
                addDependency(m_deps, candidate.lexically_normal().generic_string());
            }

            // shaderc convention: empty source_name, message in content.
            return makeResult({}, std::string("cannot find include \"") + requested +
                                  "\" (from " + requesting + ")");
        }

        void ReleaseInclude(shaderc_include_result *result) override
        {
            delete static_cast<Holder *>(result->user_data);
        }

    private:
        struct Holder
        {
            shaderc_include_result result{};
            std::string            name;
            std::string            content;
        };

        static shaderc_include_result *makeResult(std::string name, std::string content)
        {
            auto *holder = new Holder{ {}, std::move(name), std::move(content) };
            holder->result = shaderc_include_result{
                .source_name        = holder->name.c_str(),
                .source_name_length = holder->name.size(),
                .content            = holder->content.c_str(),
                .content_length     = holder->content.size(),
                .user_data          = holder,
            };
            return &holder->result;
        }

        fs::path                  m_root;
        std::vector<std::string> *m_deps;
    };

    void report(std::string *error, const std::string &msg, bool modal)
    {
        if (error) {
            *error = msg;
        } else if (modal) {
            showError(msg);
        } else {
            std::cerr << "Shader compilation error: " << msg << std::endl;
        }
    }
}

std::vector<uint32_t> compileGlslToSpirv(const std::string &fileName, shaderc_shader_kind kind,
                                         std::string *error, std::vector<std::string> *deps)
{
    addDependency(deps, fileName);

    const fs::path shaderPath = fs::path(SHADER_DIR) / fileName;
    const std::optional<std::string> src = readTextFile(shaderPath);
    if (!src || src->empty()) {
        report(error, "Shader file does not exist or is empty: " + shaderPath.string(), true);
        return {};
    }

    std::cout << "Compiling shader: " << shaderPath.string() << std::endl;

    shaderc::Compiler compiler;
    shaderc::CompileOptions opts;
    opts.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_4);
    opts.SetTargetSpirv(shaderc_spirv_version_1_6);
    opts.SetOptimizationLevel(shaderc_optimization_level_performance);
    opts.SetIncluder(std::make_unique<ShaderIncluder>(SHADER_DIR, deps));

    const shaderc::SpvCompilationResult result =
        compiler.CompileGlslToSpv(*src, kind, fileName.c_str(), opts);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        report(error, result.GetErrorMessage(), false);
        return {};
    }
    return { result.cbegin(), result.cend() };
}

VkShaderModule compileShaderModule(VkDevice device, const std::string &fileName,
                                   shaderc_shader_kind kind, std::string *error,
                                   std::vector<std::string> *deps)
{
    const std::vector<uint32_t> spirv = compileGlslToSpirv(fileName, kind, error, deps);
    if (spirv.empty()) {
        return nullptr;
    }

    const VkShaderModuleCreateInfo shaderModuleCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirv.size() * sizeof(uint32_t),
        .pCode = spirv.data()
    };

    VkShaderModule shaderModule = nullptr;
    if (vkCreateShaderModule(device, &shaderModuleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        report(error, "Failed to create shader module for " + fileName, true);
        return nullptr;
    }
    return shaderModule;
}
