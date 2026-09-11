#include "EditorUI.h"

#include <volk.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
#include <SDL3/SDL.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <string>

#include "../render/VulkanContext.h"
#include "../render/GeometryStore.h"
#include "../scene/Scene.h"
#include "../common/errors.h"

bool EditorUI::initialize(SDL_Window *window, VulkanContext &ctx,
                          VkFormat colorFormat, VkFormat depthFormat,
                          uint32_t minImageCount, uint32_t imageCount,
                          VkQueue graphicsQueue, uint32_t graphicsQueueFamily)
{
    // Imgui allovcates one combined image samlpler per texture it binds.
    // Font is the only one for now until we preview textures.

    VkDescriptorPoolSize poolSize
    {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 16
    };
    VkDescriptorPoolCreateInfo poolInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 16,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize
    };
    if (vkCreateDescriptorPool(ctx.device(), &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        showError("EditorUI: failed to create the descriptor pool");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui:ImGui::StyleColorsDark();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = "editor_layout.ini";

    if (!ImGui_ImplSDL3_InitForVulkan(window)) {
        showError("EditorUI: ImGui_ImplSDL3_InitForVulkan failed");
        return false;
    }
    ImGui_ImplVulkan_LoadFunctions(
        VK_API_VERSION_1_3,
        [](const char *name, void *userData) {
            return vkGetInstanceProcAddr(static_cast<VkInstance>(userData), name);
        },
        ctx.instance());

    VkPipelineRenderingCreateInfo renderingInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
        .depthAttachmentFormat = depthFormat
    };

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = ctx.instance();
    initInfo.PhysicalDevice = ctx.physical();
    initInfo.Device = ctx.device();
    initInfo.QueueFamily = graphicsQueueFamily;
    initInfo.Queue = graphicsQueue;
    initInfo.DescriptorPool = m_pool;
    initInfo.MinImageCount = minImageCount;
    initInfo.ImageCount = imageCount;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineRenderingCreateInfo = renderingInfo;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        showError("EditorUI: ImGui_ImplVulkan_Init failed");
        return false;
    }

    m_initialized = true;
    return true;
}

void EditorUI::shutdown(VulkanContext &ctx)
{
    if (!m_initialized) {
        return;
    }
    vkDeviceWaitIdle(ctx.device());

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (m_pool) {
        vkDestroyDescriptorPool(ctx.device(), m_pool, nullptr);
        m_pool = nullptr;
    }
    m_initialized = false;
}

