#include "VulkanContext.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

// volk and VMA must have their implementations in exactly ONE translation
// unit. This is that unit -- do not define these anywhere else.
#define VOLK_IMPLEMENTATION
#include <volk.h>
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

#include "../common/errors.h"



namespace {
    VkDebugUtilsMessengerCreateInfoEXT debugMessengerInfo()
    {
        return VkDebugUtilsMessengerCreateInfoEXT
        {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = VulkanContext::debugCallback
        };
    }
}

// ============================================================================
// lifetime
// ============================================================================

bool VulkanContext::initialize(SDL_Window *window, uint32_t apiVersion)
{
    if (!createInstance(apiVersion)) {
        showError("Error creating Vulkan instance");
        return false;
    }

    if (!createDebugMessenger()) {
        showError("Error creating the debug messenger");
        return false;
    }

    if (!createSurface(window)) {
        showError("Error creating surface");
        return false;
    }
    if (!findPhysicalDevice()) {
        showError("Error finding a suitable physical device");
        return false;
    }
    if (!findGraphicsQueue()) {
        showError("Unable to find a compatible graphics queue");
        return false;
    }
    if (!createDevice()) {
        showError("Could not create the logical GPU device");
        return false;
    }
    if (!initializeVMA(apiVersion)) {
        showError("Unable to create the Vulkan Memory Allocator");
        return false;
    }
    if (!m_uploader.initialize(m_device, m_allocator, m_gfxQueue, m_gfxQueueFamIdx)) {
        showError("Unable to create the upload engine");
        return false;
    }
    return true;
}

void VulkanContext::shutdown()
{
    // Drains outstanding uploads and releases their staging memory
    m_uploader.shutdown();

    if (m_allocator) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = nullptr;
    }
    if (m_surface) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = nullptr;
    }
    if (m_device) {
        vkDestroyDevice(m_device, nullptr);
        m_device = nullptr;
    }

    if (m_debugMessenger) {
        vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = nullptr;
    }

    if (m_instance) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = nullptr;
    }
    volkFinalize();
}

// ============================================================================
// instance / debug
// ============================================================================

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
    void * /*userData*/)
{
    if (severity < VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        return VK_FALSE;
    }

    const char *idName = callbackData->pMessageIdName ? callbackData->pMessageIdName : "";
    std::cerr << "[vk] " << idName << ": " << callbackData->pMessage << std::endl;

#ifndef NDEBUG
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        __builtin_trap();
    }
#endif
    return VK_FALSE;
}

bool VulkanContext::createDebugMessenger()
{
    // volkLoadInstance leaves this null when VK_EXT_debug_utils isn't present.
    // Not fatal: the instance-scoped pNext messenger still covered creation.
    if (!vkCreateDebugUtilsMessengerEXT) {
        std::cerr << "[warn] VK_EXT_debug_utils unavailable; "
                     "no runtime validation callback" << std::endl;
        return true;
    }

    const VkDebugUtilsMessengerCreateInfoEXT info = debugMessengerInfo();
    return vkCreateDebugUtilsMessengerEXT(m_instance, &info, nullptr,
                                          &m_debugMessenger) == VK_SUCCESS;
}
bool VulkanContext::createInstance(uint32_t apiVersion)
{
    if (volkInitialize() != VK_SUCCESS) {
        showError("Error initializing volk");
        return false;
    }

    VkApplicationInfo appInfo
    {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Learning Vulkan",
        .apiVersion = apiVersion,
    };

    uint32_t sdlExtCount = 0;
    const char *const *sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);

    std::vector<const char *> requestedExtensions{ VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
    for (uint32_t i = 0; i < sdlExtCount; ++i) {
        requestedExtensions.push_back(sdlExtensions[i]);
    }

    const std::vector<const char *> requestedLayers{ "VK_LAYER_KHRONOS_validation" };

    // Attached via pNext so the callback also covers instance creation and
    // destruction, which happen outside the messenger's own lifetime.
    VkDebugUtilsMessengerCreateInfoEXT debugInfo
    {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback
    };

    VkInstanceCreateInfo instCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = &debugInfo,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(requestedLayers.size()),
        .ppEnabledLayerNames = requestedLayers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(requestedExtensions.size()),
        .ppEnabledExtensionNames = requestedExtensions.data()
    };

    if (vkCreateInstance(&instCreateInfo, nullptr, &m_instance) != VK_SUCCESS) {
        return false;
    }
    volkLoadInstance(m_instance);
    return true;
}


bool VulkanContext::createSurface(SDL_Window *window)
{
    return SDL_Vulkan_CreateSurface(window, m_instance, nullptr, &m_surface);
}

// ============================================================================
// device
// ============================================================================

bool VulkanContext::findPhysicalDevice()
{
    uint32_t physDeviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &physDeviceCount, nullptr);
    if (!physDeviceCount) {
        return false;
    }

    std::vector<VkPhysicalDevice> physicalDevices(physDeviceCount);
    vkEnumeratePhysicalDevices(m_instance, &physDeviceCount, physicalDevices.data());

    m_physicalDevice = physicalDevices[0];   // default to the first GPU
    for (VkPhysicalDevice pDev : physicalDevices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(pDev, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            std::cout << "Found physical device: " << props.deviceName << std::endl;
            m_physicalDevice = pDev;

            break;
        }
    }

    // NOTE: the surface-format check that used to live here moved into
    // Swapchain::create -- the context has no business knowing what colour
    // format the swapchain wants.
    return m_physicalDevice != nullptr;
}

bool VulkanContext::findGraphicsQueue()
{
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties2(m_physicalDevice, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties2> queueFamProps(
        queueFamilyCount, { .sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2 });
    vkGetPhysicalDeviceQueueFamilyProperties2(m_physicalDevice, &queueFamilyCount, queueFamProps.data());

    for (uint32_t familyIdx = 0; familyIdx < queueFamProps.size(); ++familyIdx) {
        VkBool32 hasPresentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, familyIdx, m_surface, &hasPresentSupport);

        const VkQueueFamilyProperties &props = queueFamProps[familyIdx].queueFamilyProperties;
        if ((props.queueFlags & VK_QUEUE_GRAPHICS_BIT) && hasPresentSupport) {
            m_gfxQueueFamIdx = familyIdx;
            return true;
        }
    }
    return false;
}

bool VulkanContext::createDevice()
{
    // Query what the device actually supports before asking for it.
    VkPhysicalDeviceVulkan14Features supported14{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES };
    VkPhysicalDeviceVulkan13Features supported13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &supported14 };
    VkPhysicalDeviceVulkan12Features supported12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &supported13 };
    VkPhysicalDeviceFeatures2        supported  { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,          .pNext = &supported12 };
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &supported);

    if (!supported13.dynamicRendering || !supported13.synchronization2 ||
        !supported13.shaderDemoteToHelperInvocation ||
        !supported13.shaderTerminateInvocation ||
        !supported12.timelineSemaphore || !supported12.bufferDeviceAddress ||
        !supported12.descriptorIndexing || !supported12.runtimeDescriptorArray ||
        !supported12.descriptorBindingPartiallyBound ||
        !supported12.descriptorBindingSampledImageUpdateAfterBind ||
        !supported.features.multiDrawIndirect ||
        !supported.features.drawIndirectFirstInstance ||
        !supported.features.depthClamp)
    {
        showError("Physical device does not meet the feature requirements");
        return false;
    }

    // A separate chain for device creation -- do not reuse the query chain.
    VkPhysicalDeviceVulkan14Features features14
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
        .pNext = nullptr,
    };
    VkPhysicalDeviceVulkan13Features features13
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &features14,
        // SPIR-V 1.6 removed OpKill, so glslang lowers every `discard` to
        // OpDemoteToHelperInvocation (or OpTerminateInvocation, depending on
        // the construct). Both are core in 1.3 but still opt-in, and any
        // fragment shader that discards -- alpha masking in pbr.frag, the
        // selection outline -- fails module creation without them.
        // NOTE: declaration order matters here; these two sit BEFORE
        // synchronization2 in the struct, so designated initialisers have to
        // list them first.
        .shaderDemoteToHelperInvocation = VK_TRUE,
        .shaderTerminateInvocation = VK_TRUE,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    VkPhysicalDeviceVulkan12Features features12
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &features13,
        .descriptorIndexing = VK_TRUE,
        .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
        .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
        .descriptorBindingPartiallyBound = VK_TRUE,
        .runtimeDescriptorArray = VK_TRUE,
        .scalarBlockLayout = VK_TRUE,
        .timelineSemaphore = VK_TRUE,
        .bufferDeviceAddress = VK_TRUE,
    };
    VkPhysicalDeviceFeatures2 features
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &features12,
        .features
        {
            .multiDrawIndirect = VK_TRUE,
            .drawIndirectFirstInstance = VK_TRUE,
            // Shadow casters nearer the light than its near plane get clamped
            // onto it instead of clipped away. Without this they punch holes
            // in their own shadows.
            .depthClamp = VK_TRUE,
            .shaderInt64 = VK_TRUE,
        }
    };

    const std::vector<float> queuePriorities{ 1.0f };
    VkDeviceQueueCreateInfo gfxQueueInfo
    {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = m_gfxQueueFamIdx,
        .queueCount = 1,
        .pQueuePriorities = queuePriorities.data()
    };

    const std::vector<const char *> deviceExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo devCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &gfxQueueInfo,
        .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
        .ppEnabledExtensionNames = deviceExtensions.data(),
        .pEnabledFeatures = nullptr     // the chain lives in pNext
    };

    if (vkCreateDevice(m_physicalDevice, &devCreateInfo, nullptr, &m_device) != VK_SUCCESS) {
        return false;
    }

    // Load device-level entry points directly -- skips the dispatch indirection
    // volkLoadInstance leaves in place.
    volkLoadDevice(m_device);

    vkGetDeviceQueue(m_device, m_gfxQueueFamIdx, 0, &m_gfxQueue);
    if (!m_gfxQueue) {
        showError("Could not get the graphics queue");
        return false;
    }
    return true;
}

bool VulkanContext::initializeVMA(uint32_t apiVersion)
{
    VmaVulkanFunctions vmaFuncInfo{};
    VmaAllocatorCreateInfo vmaAllocInfo
    {
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = m_physicalDevice,
        .device = m_device,
        .pVulkanFunctions = &vmaFuncInfo,
        .instance = m_instance,
        .vulkanApiVersion = apiVersion
    };
    vmaImportVulkanFunctionsFromVolk(&vmaAllocInfo, &vmaFuncInfo);

    return vmaCreateAllocator(&vmaAllocInfo, &m_allocator) == VK_SUCCESS;
}

// ============================================================================
// format capabilities
// ============================================================================

VkFormatFeatureFlags VulkanContext::optimalFeatures(VkFormat format)
{
    VkFormatProperties2 props{ .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
    vkGetPhysicalDeviceFormatProperties2(m_physicalDevice, format, &props);
    return props.formatProperties.optimalTilingFeatures;
}

bool VulkanContext::supportsLinearFilter(VkFormat format)
{
    return (optimalFeatures(format) & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
}

uint32_t VulkanContext::mipLevelsFor(VkFormat format, uint32_t width, uint32_t height)
{
    const uint32_t wanted = mipLevelCount(width, height);
    if (wanted <= 1) {
        return 1;
    }

    // Blitting a mip chain needs all three
    // VK_FILTER_LINEAR in vkCmdBlitImage is only legal
    // if the SOURCE format supports linear filtering, and it is not
    // guaranteed for anything beyond the mandatory formats -- 16F and 32F
    // colour formats are where devices differ

    constexpr VkFormatFeatureFlags needed =
        VK_FORMAT_FEATURE_BLIT_SRC_BIT |
        VK_FORMAT_FEATURE_BLIT_DST_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

    const VkFormatFeatureFlags features = optimalFeatures(format);
    if ((features & needed) == needed) {
        return wanted;
    }

    if (std::find(m_warnedFormats.begin(), m_warnedFormats.end(), format) == m_warnedFormats.end()) {
        m_warnedFormats.push_back(format);
        std::cerr << "[warn] format " << static_cast<int>(format)
                  << " cannot be linear-blitted on this device"
                  << " (BLIT_SRC=" << ((features & VK_FORMAT_FEATURE_BLIT_SRC_BIT) != 0)
                  << " BLIT_DST=" << ((features & VK_FORMAT_FEATURE_BLIT_DST_BIT) != 0)
                  << " FILTER_LINEAR=" << ((features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0)
                  << "); falling back to a single mip level" << std::endl;
    }
    return 1;
}

// ============================================================================
// buffers
// ============================================================================

GPUBuffer VulkanContext::createBuffer(VkBufferUsageFlags usage, size_t byteSize,
                                      bool mappable, VmaMemoryUsage memoryUsage) const
{
    if (byteSize == 0) {
        return GPUBuffer{};
    }

    VkBufferCreateInfo buffInfo
    {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = byteSize,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    // HOST_ACCESS_SEQUENTIAL_WRITE means we intend to map and write
    // sequentially. mappable == false evaluates to 0: never CPU-mapped.
    VmaAllocationCreateInfo allocInfo
    {
        .flags = mappable ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT : 0u,
        .usage = memoryUsage
    };

    GPUBuffer gpuBuff;
    if (vmaCreateBuffer(m_allocator, &buffInfo, &allocInfo,
                        &gpuBuff.vkBuffer, &gpuBuff.allocation, nullptr) != VK_SUCCESS)
    {
        return GPUBuffer{};
    }

    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        VkBufferDeviceAddressInfo bdaInfo
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = gpuBuff.vkBuffer
        };
        gpuBuff.deviceAddress = vkGetBufferDeviceAddress(m_device, &bdaInfo);
    }
    return gpuBuff;
}

void VulkanContext::destroyBuffer(GPUBuffer &buffer) const
{
    if (buffer.vkBuffer) {
        vmaDestroyBuffer(m_allocator, buffer.vkBuffer, buffer.allocation);
    }
    buffer = GPUBuffer{};
}

void VulkanContext::mapCopyBufferData(const GPUBuffer &buffer, size_t bufferOffset,
                                      const void *data, size_t byteSize) const
{
    if (!byteSize) {
        return;
    }

    void *buffPtr = nullptr;
    if (vmaMapMemory(m_allocator, buffer.allocation, &buffPtr) != VK_SUCCESS) {
        showError("Unable to map buffer memory");
        return;
    }
    std::memcpy(static_cast<char *>(buffPtr) + bufferOffset, data, byteSize);
    vmaUnmapMemory(m_allocator, buffer.allocation);
}

// ============================================================================
// images
// ============================================================================

bool VulkanContext::createImage2D(VkCommandBuffer commandBuffer, const void *imageData,
                                  uint32_t width, uint32_t height, VkFormat format,
                                  GPUImage &outImage)
{
    const uint32_t bytesPerPixel = formatBytesPerPixel(format);
    if (bytesPerPixel == 0) {
        showError("createImage2D: unsupported format");
        return false;
    }
    outImage = GPUImage{};

    // Ask for the full chain, take what the format can actually do. A format
    // without linear blit support gets one level rather than an invalid blit.
    const uint32_t mipLevels = mipLevelsFor(format, width, height);

    VkImageCreateInfo imageInfo
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent{ .width = width, .height = height, .depth = 1 },
        .mipLevels = mipLevels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
         VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
         VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    VmaAllocationCreateInfo allocInfo{ .usage = VMA_MEMORY_USAGE_AUTO };
    if (vmaCreateImage(m_allocator, &imageInfo, &allocInfo,
                       &outImage.image, &outImage.allocation, nullptr) != VK_SUCCESS)
    {
        showError("Error creating image");
        outImage = GPUImage{};
        return false;
    }

    VkImageViewCreateInfo imgViewInfo
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = outImage.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange
        {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = mipLevels,
            .layerCount = 1
        }
    };

    if (vkCreateImageView(m_device, &imgViewInfo, nullptr, &outImage.imageView) != VK_SUCCESS) {
        showError("Error creating image view");
        // Roll back the image so we never hand back a half-built GPUImage --
        // that is exactly how a null imageView reaches a descriptor write.
        vmaDestroyImage(m_allocator, outImage.image, outImage.allocation);
        outImage = GPUImage{};
        return false;
    }

    // UNDEFINED -> TRANSFER_DST for the upload.
    VkImageMemoryBarrier2 transferBarrier
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .image = outImage.image,
        .subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = mipLevels, .layerCount = 1 }
    };
    VkDependencyInfo transferDep
    {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &transferBarrier
    };
    vkCmdPipelineBarrier2(commandBuffer, &transferDep);

    const size_t byteSize = static_cast<size_t>(width) * height * bytesPerPixel;
    GPUBuffer staging = createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, byteSize,
                                     true, VMA_MEMORY_USAGE_AUTO_PREFER_HOST);
    if (!staging.vkBuffer) {
        showError("Error creating image staging buffer");
        destroyImage(outImage);
        return false;
    }
    mapCopyBufferData(staging, 0, imageData, byteSize);

    // The uploader frees it once this command buffer has executed, so the
    // caller neither tracks it nor waits for it.
    m_uploader.trackBuffer(commandBuffer, staging);

    VkBufferImageCopy buffImageCopy
    {
        .imageSubresource{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 },
        .imageExtent{ .width = width, .height = height, .depth = 1 }
    };
    vkCmdCopyBufferToImage(commandBuffer, staging.vkBuffer, outImage.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buffImageCopy);

    // Mip chain by successive halving blits. Skipped entirely when mipLevels
    // came back as 1
    int32_t mipWidth  = static_cast<int32_t>(width);
    int32_t mipHeight = static_cast<int32_t>(height);

    for (uint32_t level = 1; level < mipLevels; ++level) {
        VkImageMemoryBarrier2 toSrc
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .image = outImage.image,
            .subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .baseMipLevel = level - 1, .levelCount = 1, .layerCount = 1 }
        };
        VkDependencyInfo srcDep
        {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &toSrc
        };
        vkCmdPipelineBarrier2(commandBuffer, &srcDep);

        const int32_t nextWidth  = mipWidth  > 1 ? mipWidth  / 2 : 1;
        const int32_t nextHeight = mipHeight > 1 ? mipHeight / 2 : 1;

        VkImageBlit2 blit
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
            .srcSubresource{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .mipLevel = level - 1, .baseArrayLayer = 0, .layerCount = 1 },
            .srcOffsets{ { 0, 0, 0 }, { mipWidth, mipHeight, 1 } },
            .dstSubresource{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .mipLevel = level, .baseArrayLayer = 0, .layerCount = 1 },
            .dstOffsets{ { 0, 0, 0 }, { nextWidth, nextHeight, 1 } }
        };
        VkBlitImageInfo2 blitInfo
        {
            .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
            .srcImage = outImage.image,
            .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .dstImage = outImage.image,
            .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .regionCount = 1,
            .pRegions = &blit,
            .filter = VK_FILTER_LINEAR
        };
        vkCmdBlitImage2(commandBuffer, &blitInfo);

        mipWidth  = nextWidth;
        mipHeight = nextHeight;
    }

    // Everything but the last level is TRANSFER_SRC; the last is TRANSFER_DST.
    const std::array<VkImageMemoryBarrier2, 2> readBarriers
    {
        VkImageMemoryBarrier2
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image = outImage.image,
            .subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .baseMipLevel = 0, .levelCount = mipLevels - 1, .layerCount = 1 }
        },
        VkImageMemoryBarrier2
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image = outImage.image,
            .subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .baseMipLevel = mipLevels - 1, .levelCount = 1, .layerCount = 1 }
        }
    };
    VkDependencyInfo readDep
    {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = mipLevels > 1 ? 2u : 1u,
        .pImageMemoryBarriers = mipLevels > 1 ? readBarriers.data() : &readBarriers[1]
    };
    vkCmdPipelineBarrier2(commandBuffer, &readDep);

    outImage.mipLevels = mipLevels;
    return true;
}

void VulkanContext::destroyImage(GPUImage &image) const
{
    if (image.imageView) {
        vkDestroyImageView(m_device, image.imageView, nullptr);
    }
    if (image.image) {
        vmaDestroyImage(m_allocator, image.image, image.allocation);
    }
    image = GPUImage{};
}

// ============================================================================
// uploads
// ============================================================================

VkCommandBuffer VulkanContext::beginUpload()
{
    return m_uploader.begin();
}

Uploader::Ticket VulkanContext::submitUpload()
{
    return m_uploader.submit();
}