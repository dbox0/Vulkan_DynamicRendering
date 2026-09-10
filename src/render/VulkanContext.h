#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>
#include <cstddef>
#include "../common/gpu_types.h"

struct SDL_Window;

// Owns the Vulkan device and everything with device lifetime: instance,
// physical device, surface, queue, VMA allocator, transient command pool.

class VulkanContext
{
public:
    VulkanContext() = default;
    VulkanContext(const VulkanContext &) = delete;
    VulkanContext &operator=(const VulkanContext &) = delete;

    bool initialize(SDL_Window *window, uint32_t apiVersion);
    void shutdown();

    // --- accessors -------------------------------------------------------
    VkInstance       instance()   const { return m_instance; }
    VkPhysicalDevice physical()   const { return m_physicalDevice; }
    VkDevice         device()     const { return m_device; }
    VkSurfaceKHR     surface()    const { return m_surface; }
    VmaAllocator     allocator()  const { return m_allocator; }
    VkQueue          gfxQueue()   const { return m_gfxQueue; }
    uint32_t         gfxFamily()  const { return m_gfxQueueFamIdx; }

    // --- buffers ---------------------------------------------------------
    GPUBuffer createBuffer(VkBufferUsageFlags usage, size_t byteSize,
                           bool mappable, VmaMemoryUsage memoryUsage) const;
    void destroyBuffer(GPUBuffer &buffer) const;
    void mapCopyBufferData(const GPUBuffer &buffer, size_t bufferOffset,
                           const void *data, size_t byteSize) const;

    // --- images ----------------------------------------------------------

    bool createImage2D(VkCommandBuffer commandBuffer, const unsigned char *imageData,
                       uint32_t width, uint32_t height, int channels,
                       GPUImage &outImage, GPUBuffer &outStagingBuffer) const;
    void destroyImage(GPUImage &image) const;

    // --- transient (load-time) command buffers ---------------------------
    // endTransient does a full vkQueueWaitIdle!

    VkCommandBuffer beginTransient() const;
    void            endTransient(VkCommandBuffer commandBuffer) const;

private:
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
        void *userData);

    bool createInstance(uint32_t apiVersion);

    bool createDebugMessenger();

    bool createSurface(SDL_Window *window);
    bool findPhysicalDevice();
    bool findGraphicsQueue();
    bool createDevice();
    bool initializeVMA(uint32_t apiVersion);
    bool createTransientPool();

    VkInstance       m_instance       = nullptr;
    VkPhysicalDevice m_physicalDevice = nullptr;
    VkDevice         m_device         = nullptr;
    VkSurfaceKHR     m_surface        = nullptr;
    VmaAllocator     m_allocator      = nullptr;
    VkQueue          m_gfxQueue       = nullptr;
    uint32_t         m_gfxQueueFamIdx = UINT32_MAX;
    VkCommandPool    m_transientPool  = nullptr;
};