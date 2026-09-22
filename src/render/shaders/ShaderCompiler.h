#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <shaderc/shaderc.h>   // shaderc_shader_kind
#include <cstdint>
#include <string>
#include <vector>

// GLSL under SHADER_DIR -> SPIR-V -> VkShaderModule, compiled at runtime.
//
// #include "x" resolves against the including file's directory, then SHADER_DIR;
// #include <x> resolves against SHADER_DIR only.
//
// With error set, failures are written there instead of popping a modal box
// hot reload must never block the frame loop on a typo.
//
// With deps set, it receives every file the compile opened (the root file
// plus all includes, SHADER_DIR-relative, forward slashes)
// also on failure, including includes that could not be found.

std::vector<uint32_t> compileGlslToSpirv(const std::string &fileName, shaderc_shader_kind kind,
                                         std::string *error = nullptr,
                                         std::vector<std::string> *deps = nullptr);

VkShaderModule compileShaderModule(VkDevice device, const std::string &fileName,
                                   shaderc_shader_kind kind, std::string *error = nullptr,
                                   std::vector<std::string> *deps = nullptr);
