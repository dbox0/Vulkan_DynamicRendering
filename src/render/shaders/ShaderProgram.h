#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <functional>
#include <string>
#include <vector>

// One vertex/fragment pair and the pipelines built from it.
//
// Each pass describes its own programs; the renderer only keeps the list, so
// it can compile everything at startup and rebuild exactly the affected
// pipelines on hot reload. The pointers refer into the owning pass, which
// has to outlive the list.
struct ShaderProgram
{
    const char              *vertFile;
    const char              *fragFile;
    VkShaderModule          *vertModule;
    VkShaderModule          *fragModule;
    std::vector<VkPipeline*> pipelines;
    std::function<bool()>    build;
};

// Compiles everything instead of stopping at the first failure, so one run
// reports every broken shader.
bool compileShaderPrograms(VkDevice device, std::vector<ShaderProgram> &programs);

// Recompiles and rebuilds only the programs that use one of these files.
// A program that fails to compile keeps running on its old pipeline.
void reloadShaderPrograms(VkDevice device, std::vector<ShaderProgram> &programs,
                          const std::vector<std::string> &changedFiles);
