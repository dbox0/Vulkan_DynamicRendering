#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <shaderc/shaderc.h>   // shaderc_shader_kind
#include <string>

// GLSL under SHADER_DIR -> SPIR-V -> VkShaderModule, compiled at runtime.
//
// With error set, failures are written there instead of popping a modal
// box -- hot reload must never block the frame loop on a typo.
VkShaderModule compileShaderModule(VkDevice device, const std::string &fileName,
                                   shaderc_shader_kind kind, std::string *error = nullptr);
