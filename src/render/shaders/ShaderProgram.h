#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <shaderc/shaderc.h>
#include <functional>
#include <string>
#include <vector>

// One or more shader stages and the pipelines built from them.
//
// Each pass describes its own programs; the renderer only keeps the list, so
// it can compile everything at startup and rebuild exactly the affected
// pipelines on hot reload. The pointers refer into the owning pass, which
// has to outlive the list.
struct ShaderStage
{
    const char         *file;
    shaderc_shader_kind kind;
    VkShaderModule     *module;
};

struct ShaderProgram
{
    std::vector<ShaderStage> stages;
    std::vector<VkPipeline*> pipelines;
    std::function<bool()>    build;
    std::vector<std::string> dependencies;
};

ShaderProgram graphicsProgram(const char *vertFile, const char *fragFile,
                              VkShaderModule *vertModule, VkShaderModule *fragModule,
                              std::vector<VkPipeline*> pipelines, std::function<bool()> build);

ShaderProgram computeProgram(const char *file, VkShaderModule *module,
                             std::vector<VkPipeline*> pipelines, std::function<bool()> build);

// Depth-only pipelines: no fragment stage
ShaderProgram vertexProgram(const char *vertFile, VkShaderModule *vertModule,
                            std::vector<VkPipeline*> pipelines, std::function<bool()> build);

bool dependsOnAny(const std::vector<std::string> &dependencies,
                  const std::vector<std::string> &changedFiles);

void mergeDependencies(std::vector<std::string> &dst, const std::vector<std::string> &src);

// Compiles everything instead of stopping at the first failure, so one run
// reports every broken shader.
bool compileShaderPrograms(VkDevice device, std::vector<ShaderProgram> &programs);

// Recompiles and rebuilds only the programs that read one of these files,
// directly or through an #include.
// A program that fails to compile keeps running on its old pipeline.
void reloadShaderPrograms(VkDevice device, std::vector<ShaderProgram> &programs,
                          const std::vector<std::string> &changedFiles);
