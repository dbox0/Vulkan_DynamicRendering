#include "ShaderProgram.h"

#include <volk.h>
#include <algorithm>
#include <iostream>
#include <utility>

#include "ShaderCompiler.h"

namespace {
    bool dependsOnAny(const ShaderProgram &program, const std::vector<std::string> &changedFiles)
    {
        return std::ranges::any_of(changedFiles, [&](const std::string &file) {
            return std::ranges::find(program.dependencies, file) != program.dependencies.end();
        });
    }

    void mergeInto(std::vector<std::string> &dst, const std::vector<std::string> &src)
    {
        for (const std::string &file : src) {
            if (std::ranges::find(dst, file) == dst.end()) {
                dst.push_back(file);
            }
        }
    }
}

bool compileShaderPrograms(VkDevice device, std::vector<ShaderProgram> &programs)
{
    bool ok = true;
    for (ShaderProgram &program : programs) {
        program.dependencies.clear();
        *program.vertModule = compileShaderModule(device, program.vertFile, shaderc_vertex_shader,
                                                  nullptr, &program.dependencies);
        *program.fragModule = compileShaderModule(device, program.fragFile, shaderc_fragment_shader,
                                                  nullptr, &program.dependencies);
        ok = ok && *program.vertModule && *program.fragModule;
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
        if (!dependsOnAny(program, changedFiles)) {
            continue;
        }

        // Both stages compile before anything live is touched.
        // frag stage is attempted even when vert failed, so its includes are tracked too.
        std::string vertError, fragError;
        std::vector<std::string> deps;
        VkShaderModule vert = compileShaderModule(device, program.vertFile, shaderc_vertex_shader,
                                                  &vertError, &deps);
        VkShaderModule frag = compileShaderModule(device, program.fragFile, shaderc_fragment_shader,
                                                  &fragError, &deps);
        if (!vert || !frag) {
            // Keep watching the old files as well as whatever the broken
            // version tried to include, so fixing either one retries.
            mergeInto(program.dependencies, deps);

            std::cerr << "[hot reload] " << program.vertFile << " / " << program.fragFile
                      << " failed, keeping the old pipeline:\n" << vertError << fragError << std::endl;
            if (vert) {
                vkDestroyShaderModule(device, vert, nullptr);
            }
            if (frag) {
                vkDestroyShaderModule(device, frag, nullptr);
            }
            continue;
        }
        // Both in-flight frames may still reference the old pipelines. One
        // wait covers every program an edited include touched
        if (!deviceIdle) {
            vkDeviceWaitIdle(device);
            deviceIdle = true;
        }

        // After the swap, vert/frag hold the OLD modules.
        std::swap(*program.vertModule, vert);
        std::swap(*program.fragModule, frag);

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
            // Swap back: vert/frag now hold the NEW modules
            std::swap(*program.vertModule, vert);
            std::swap(*program.fragModule, frag);
            mergeInto(program.dependencies, deps);
        }
        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);

        std::cout << "[hot reload] " << (built ? "rebuilt " : "pipeline build failed, rolled back ")
                  << program.vertFile << " / " << program.fragFile << std::endl;
    }
}
