#include "ShaderProgram.h"

#include <volk.h>
#include <algorithm>
#include <iostream>
#include <utility>

#include "ShaderCompiler.h"

bool compileShaderPrograms(VkDevice device, std::vector<ShaderProgram> &programs)
{
    bool ok = true;
    for (ShaderProgram &program : programs) {
        *program.vertModule = compileShaderModule(device, program.vertFile, shaderc_vertex_shader);
        *program.fragModule = compileShaderModule(device, program.fragFile, shaderc_fragment_shader);
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

    for (ShaderProgram &program : programs) {
        const bool touched = std::ranges::any_of(changedFiles, [&](const std::string &f) {
            return f == program.vertFile || f == program.fragFile;
        });
        if (!touched) {
            continue;
        }

        // Compile both stages before touching anything live
        std::string error;
        VkShaderModule vert = compileShaderModule(device, program.vertFile, shaderc_vertex_shader, &error);
        VkShaderModule frag = vert ? compileShaderModule(device, program.fragFile, shaderc_fragment_shader, &error)
                                   : nullptr;
        if (!vert || !frag) {
            std::cerr << "[hot reload] " << program.vertFile << " / " << program.fragFile
                      << " failed, keeping the old pipeline:\n" << error << std::endl;
            if (vert) {
                vkDestroyShaderModule(device, vert, nullptr);
            }
            continue;
        }

        // Both in-flight frames may still reference the old pipelines.
        vkDeviceWaitIdle(device);

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
        if (!built) {
            // Swap back: vert/frag now hold the NEW modules, which lost.
            std::swap(*program.vertModule, vert);
            std::swap(*program.fragModule, frag);
        }
        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);

        std::cout << "[hot reload] " << (built ? "rebuilt " : "pipeline build failed, rolled back ")
                  << program.vertFile << " / " << program.fragFile << std::endl;
    }
}
