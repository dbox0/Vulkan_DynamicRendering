#pragma once

#define VK_NO_PROTOTYPES
#include <SDL3/SDL.h>
#include <string>
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <filesystem>
#include <vk_mem_alloc.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <shaderc/shaderc.hpp>
#include "structs.h"
#include "external/tiny_gltf_v3.h"

struct SDL_Window;
struct VmaAllocator_T;
typedef struct VmaAllocator_T* VmaAllocator;
struct VmaAllocation_T;
typedef struct VmaAllocation_T* VmaAllocation;


struct FrameConstants {
    uint64_t vertexBufferAddress = 0;
    uint64_t materialBufferAddress = 0;
    uint64_t renderItemsAddress = 0;
};


struct RenderItem
{
    glm::mat4 wvp;
    glm::mat4 worldMatrix;
    uint32_t materialIndex = 0;
};

struct FrameResources
{
    VkCommandPool commandPool = nullptr;
    VkCommandBuffer commandBuffer = nullptr;
    VkSemaphore imageAcquiredSemaphore = nullptr;
    VkDescriptorSet descSet = nullptr;
    GPUBuffer indirectDrawBuffer;
    GPUBuffer renderItemBuffer;
    VkDrawIndexedIndirectCommand *indirectDrawPtr = nullptr;
    RenderItem *renderItemPtr = nullptr;
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
    VkDevice m_device = nullptr;
    VkSurfaceKHR surface = nullptr;
    VmaAllocator m_vmaAllocator = nullptr;

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


    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;

    // GPU resources
    uint32_t m_purplePixelImageId = 0;
    uint32_t m_vertexBufferId = 0;
    uint32_t m_indexBufferId = 0;
    uint32_t m_matBufferId = 0;

    std::vector<GPUImage> m_images;
    std::vector<VkSampler> m_samplers;
    std::vector<Texture> m_textures;
    std::vector<GPUBuffer> m_GpuBuffers;
    std::vector<Material> m_materials;

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
    void submitTransientCommandBuffer(VkCommandBuffer commandBuffer);
    VkCommandBuffer startTransientCommandBuffer();
    void render();
    VkCommandPool m_commandPool;
    std::pair<uint32_t, GPUBuffer> createImage(VkCommandBuffer commandBuffer, unsigned char *imageData, uint32_t width, uint32_t height, int channels);

    GPUBuffer createBuffer(VkBufferUsageFlags usage, size_t byteSize, bool mappable, VmaMemoryUsage memoryUsage);
    void mapCopyBufferData(const GPUBuffer &buffer, size_t bufferOffset, void *data, size_t byteSize);
    void loadGltf(const std::string &filepath);
    std::vector<Image> loadImages(const tg3_model &model, const std::filesystem::path &imageDir);
    std::vector<uint32_t> uploadImages(const std::vector<Image> &images);

    std::vector<uint32_t> loadSamplers(const tg3_model &model);
    std::vector<uint32_t> loadTextures(const tg3_model &model,const std::vector<uint32_t> imageIds, std::vector<uint32_t> samplerIds);
    std::vector<uint32_t> loadMaterials(const tg3_model &model,const std::vector<uint32_t> textureIds);
    std::vector<uint32_t> loadMeshes(const tg3_model &model, const std::vector<uint32_t> materialIds);

public:
    bool initialize();
    void shutdown();
    void run();
    bool loadData();
};