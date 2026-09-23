#include "ShaderProgram.h"

#include <volk.h>
#include <algorithm>
#include <iostream>
#include <utility>

#include "ShaderCompiler.h"

namespace {
    std::string programName(const ShaderProgram &program)
    {
        std::string name;
        for (const ShaderStage &stage : program.stages) {
            name += name.empty() ? stage.file : std::string(" / ") + stage.file;
        }
        return name;
    }
}

ShaderProgram graphicsProgram(const char *vertFile, const char *fragFile,
                              VkShaderModule *vertModule, VkShaderModule *fragModule,
                              std::vector<VkPipeline*> pipelines, std::function<bool()> build)
{
    return ShaderProgram{
        .stages    = { { vertFile, shaderc_vertex_shader, vertModule },
                       { fragFile, shaderc_fragment_shader, fragModule } },
        .pipelines = std::move(pipelines),
        .build     = std::move(build),
    };
}

ShaderProgram computeProgram(const char *file, VkShaderModule *module,
                             std::vector<VkPipeline*> pipelines, std::function<bool()> build)
{
    return ShaderProgram{
        .stages    = { { file, shaderc_compute_shader, module } },
        .pipelines = std::move(pipelines),
        .build     = std::move(build),
    };
}

ShaderProgram vertexProgram(const char *vertFile, VkShaderModule *vertModule,
                            std::vector<VkPipeline*> pipelines, std::function<bool()> build)
{
    return ShaderProgram{
        .stages    = { { vertFile, shaderc_vertex_shader, vertModule } },
        .pipelines = std::move(pipelines),
        .build     = std::move(build),
    };
}


bool dependsOnAny(const std::vector<std::string> &dependencies,
                  const std::vector<std::string> &changedFiles)
{
    return std::ranges::any_of(changedFiles, [&](const std::string &file) {
        return std::ranges::find(dependencies, file) != dependencies.end();
    });
}

void mergeDependencies(std::vector<std::string> &dst, const std::vector<std::string> &src)
{
    for (const std::string &file : src) {
        if (std::ranges::find(dst, file) == dst.end()) {
            dst.push_back(file);
        }
    }
}

bool compileShaderPrograms(VkDevice device, std::vector<ShaderProgram> &programs)
{
    bool ok = true;
    for (ShaderProgram &program : programs) {
        program.dependencies.clear();
        for (const ShaderStage &stage : program.stages) {
            *stage.module = compileShaderModule(device, stage.file, stage.kind,
                                                nullptr, &program.dependencies);
            ok = ok && *stage.module;
        }
    }
    return ok;
}

void reloadShaderPrograms(VkDevice device, std::vector<ShaderProgram> &programs,
                          const std::vector<std::string> &changedFiles)
{
    if (changedFiles.empty()) {
        return;
    }

    bool deviceIdle = false;

    for (ShaderProgram &program : programs) {
        if (!dependsOnAny(program.dependencies, changedFiles)) {
            continue;
        }

        // Every stage compiles before anything live is touched.
        // Later stages are attempted even when an earlier one failed, so their includes are tracked too.
        std::string errors;
        std::vector<std::string> deps;
        std::vector<VkShaderModule> modules;
        bool compiled = true;
        for (const ShaderStage &stage : program.stages) {
            std::string error;
            modules.push_back(compileShaderModule(device, stage.file, stage.kind, &error, &deps));
            if (!modules.back()) {
                compiled = false;
                errors += error;
            }
        }
        if (!compiled) {
            // Keep watching the old files as well as whatever the broken
            // version tried to include, so fixing either one retries.
            mergeDependencies(program.dependencies, deps);

            std::cerr << "[hot reload] " << programName(program)
                      << " failed, keeping the old pipeline:\n" << errors << std::endl;
            for (VkShaderModule module : modules) {
                vkDestroyShaderModule(device, module, nullptr);
            }
            continue;
        }
        // Both in-flight frames may still reference the old pipelines. One
        // wait covers every program an edited include touched
        if (!deviceIdle) {
            vkDeviceWaitIdle(device);
            deviceIdle = true;
        }

        // After the swap, modules holds the OLD modules.
        for (size_t i = 0; i < modules.size(); ++i) {
            std::swap(*program.stages[i].module, modules[i]);
        }

        std::vector<VkPipeline> oldPipelines;
        for (VkPipeline *p : program.pipelines) {
            oldPipelines.push_back(std::exchange(*p, nullptr));
        }

        const bool built = program.build();

        // Whichever set lost gets destroyed: the old one on success, the
        // (possibly partial) new one on failure, with the old one put back.
        for (size_t i = 0; i < program.pipelines.size(); ++i) {
            VkPipeline &live = *program.pipelines[i];
            const VkPipeline dead = built ? oldPipelines[i] : std::exchange(live, oldPipelines[i]);
            if (dead) {
                vkDestroyPipeline(device, dead, nullptr);
            }
        }
        if (built) {
            program.dependencies = std::move(deps);
        } else {
            // Swap back: modules now holds the NEW modules
            for (size_t i = 0; i < modules.size(); ++i) {
                std::swap(*program.stages[i].module, modules[i]);
            }
            mergeDependencies(program.dependencies, deps);
        }
        for (VkShaderModule module : modules) {
            vkDestroyShaderModule(device, module, nullptr);
        }

        std::cout << "[hot reload] " << (built ? "rebuilt " : "pipeline build failed, rolled back ")
                  << programName(program) << std::endl;
    }
}
