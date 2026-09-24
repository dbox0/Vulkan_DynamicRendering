#pragma once
#define VK_NO_PROTOTYPES
#include <cmath>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>
#include <cstddef>
#include <vector>
#include "gpu_types.h"
#include "Uploader.h"

struct SDL_Window;

// Owns the Vulkan device and everything with device lifetime: instance,
// physical device, surface, queue, VMA allocator, upload engine.

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
    Uploader        &uploader()         { return m_uploader; }

    // --- buffers ---------------------------------------------------------
    GPUBuffer createBuffer(VkBufferUsageFlags usage, size_t byteSize,
                           bool mappable, VmaMemoryUsage memoryUsage) const;
    void destroyBuffer(GPUBuffer &buffer) const;
    void mapCopyBufferData(const GPUBuffer &buffer, size_t bufferOffset,
                           const void *data, size_t byteSize) const;

    // --- images ----------------------------------------------------------
    // Records the upload (and the mip chain) into commandBuffer. The staging
    // buffer is handed to the uploader, so the caller has nothing to clean up
    // and nothing to wait for.
    bool createImage2D(VkCommandBuffer commandBuffer, const void *imageData,
                       uint32_t width, uint32_t height, VkFormat format,
                       GPUImage &outImage);

    // The full chain the dimensions would allow. What an image actually gets
    // is GPUImage::mipLevels -> see mipLevelsFor().
    static uint32_t mipLevelCount(uint32_t width, uint32_t height)
    {
        return 1u + static_cast<uint32_t>(std::floor(std::log2(std::max(width, height))));
    }

    // mipLevelCount clamped to what the format can actually do on the
    // device: generating mips by blitting needs BLIT_SRC, BLIT_DST and
    // SAMPLED_IMAGE_FILTER_LINEAR on optimal tiling. Returns 1 when any of
    // them is missing.
    uint32_t mipLevelsFor(VkFormat format, uint32_t width, uint32_t height);

    // Whether a sampler may use VK_FILTER_LINEAR with this format at all.
    bool supportsLinearFilter(VkFormat format);

    void destroyImage(GPUImage &image) const;

    // --- render targets --------------------------------------------------
    // An image a pass draws into (and usually a later pass samples): one mip,
    // one layer, dedicated memory, nothing uploaded. The view's aspect is
    // derived from the format, so colour and depth targets use the same call.
    // Released with destroyImage().
    bool createRenderTarget(uint32_t width, uint32_t height, VkFormat format,
                        VkImageUsageFlags usage, GPUImage &outImage,
                        uint32_t layers = 1,
                        VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D) const;

    static bool isDepthFormat(VkFormat format);

    // --- load-time / streaming uploads -----------------------------------
    // begin -> record -> submit. submit() does not wait: it returns a ticket
    // that can be polled or ignored.

    VkCommandBuffer beginUpload();
    Uploader::Ticket submitUpload();

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
        void *userData);

private:


    bool createInstance(uint32_t apiVersion);

    bool createDebugMessenger();

    bool createSurface(SDL_Window *window);
    bool findPhysicalDevice();
    bool findGraphicsQueue();
    bool createDevice();
    bool initializeVMA(uint32_t apiVersion);
    VkFormatFeatureFlags optimalFeatures(VkFormat format);

    VkInstance       m_instance       = nullptr;
    VkPhysicalDevice m_physicalDevice = nullptr;
    VkDevice         m_device         = nullptr;
    VkSurfaceKHR     m_surface        = nullptr;
    VmaAllocator     m_allocator      = nullptr;
    VkQueue          m_gfxQueue       = nullptr;
    uint32_t         m_gfxQueueFamIdx = UINT32_MAX;
    Uploader         m_uploader;
    VkDebugUtilsMessengerEXT m_debugMessenger = nullptr;

    // Formats already reported as not mippable, so the warning is printed
    // once per format instead of once per texture.
    std::vector<VkFormat> m_warnedFormats;

};