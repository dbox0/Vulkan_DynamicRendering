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

    SDL_Window* window = nullptr;
    uint32_t width = 1280;
    uint32_t height = 720;
    bool running = false;
    uint64_t frameIndex = 0;
    uint64_t nextSignalValue = MaxFramesInFlight + 1;

    // Vulkan CORE
    VkInstance vulkanInstance = nullptr;
    VkPhysicalDevice physicalDevice = nullptr;
    VkDevice device = nullptr;
    VkSurfaceKHR surface = nullptr;
    VmaAllocator vmaAllocator = nullptr;

public:
    bool initialize();
    void shutdown();
    void run();
};