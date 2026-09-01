#pragma once

#define VK_NO_PROTOTYPES
#include <SDL3/SDL.h>
#include <string>
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <shaderc/shaderc.hpp>

struct SDL_Window;
struct VmaAllocator_T;
typedef struct VmaAllocator_T* VmaAllocator;
struct VmaAllocation_T;
typedef struct VmaAllocation_T* VmaAllocation;

struct FrameResources {
    VkCommandPool commandPool = nullptr;
    VkCommandBuffer commandBuffer = nullptr;
    VkSemaphore imageAcquiredSemaphore = nullptr;
};

class Application {
    constexpr static uint32_t VulkanVersion{VK_API_VERSION_1_4};
    constexpr static uint32_t MaxFramesInFlight{2};
    constexpr static VkFormat swapchainFormat{VK_FORMAT_B8G8R8A8_SRGB};
    constexpr static VkFormat depthFormat{VK_FORMAT_D32_SFLOAT};

    // ================================================================================//
    //SDL
    SDL_Window* window = nullptr;
    uint32_t width = 1280;
    uint32_t height = 720;
    bool running = false;
    uint64_t frameIndex = 0;
    uint64_t nextSignalValue = MaxFramesInFlight + 1;

    // ================================================================================//
    // Vulkan CORE
    VkInstance vulkanInstance = nullptr;
    VkPhysicalDevice physicalDevice = nullptr;
    VkDevice device = nullptr;
    VkSurfaceKHR surface = nullptr;
    VmaAllocator vmaAllocator = nullptr;

    // ================================================================================//
    // Queue
    uint32_t gfxQueueFamIdx = UINT32_MAX;
    VkQueue gfxQueue = nullptr;

    // ================================================================================//
    // swapchain  (List of Images we get from the OS that we can draw Frame over frame..)
    VkSwapchainKHR swapchain = nullptr;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkSemaphore> renderCompleteSemaphores;
    bool requireSwapchainRecreate = false;
    uint32_t swapchainWidth = 0;
    uint32_t swapchainHeight = 0;

    // Depth Buffer
    VkImage depthImage = nullptr;
    VkImageView depthImageView = nullptr;
    VmaAllocation depthImageAllocation = nullptr;

    // ================================================================================//
    // graphics pipeline
    VkPipelineLayout pipelineLayout = nullptr;
    VkPipeline pipeline = nullptr;

    //shader resources
    VkShaderModule vertexShaderModule = nullptr;
    VkShaderModule fragmentShaderModule = nullptr;

    // frame and sync resources
    VkSemaphore timelineSemaphore = nullptr;
    std::array<FrameResources, MaxFramesInFlight> frameResources;

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
      VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
      VkDebugUtilsMessageTypeFlagsEXT messageType,
      const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
      void *pUserData);

    void showError(const std::string &errorMsg) const;

    bool initializeVulkan();
    bool createVulkanInstance();
    bool createSurface();
    VkPhysicalDevice findPhysicalDevice();
    bool findGraphicsQueue();
    bool createDevice(VkPhysicalDevice phyiscalDevice);
    bool initializeVMA();
    bool createSwapchain(uint32_t width, uint32_t height);
    void destroySwapchain();
    VkShaderModule createShaderModule(const std::string &fileName, shaderc_shader_kind kind) const;
    bool createShaders();
    VkPipeline createGraphicsPipeline();
    bool createSyncResources();
    bool createCommandBuffers();
    void render();

public:
    bool initialize();
    void shutdown();
    void run();
};