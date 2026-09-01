#include "application.h"

#include <iostream>
#include <ostream>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#define VOLK_IMPLEMENTATION
#include <volk.h>
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>



void Application::showError(const std::string &errorMessasge) const
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", errorMessasge.c_str(), window);
}

void Application::run() {
    running = true;
    while (running) {
        SDL_Event event{0};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
                break;
            }
            else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
                width = event.window.data1;
                height = event.window.data2;
                break;
            }
        }
        render();
    }
}

bool Application::initialize() {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        window = SDL_CreateWindow("Learning Vulkan",
            width, height,
            SDL_WINDOW_VULKAN |
            SDL_WINDOW_RESIZABLE);
        if (!window) {
            showError("Error creating window");
            return false;
        }
        if (!initializeVulkan()) {
            return false;
        }
    }else {
        showError("Unable to initialize SDL3");
    }
    return true;
}
bool Application::createVulkanInstance() {
    if (volkInitialize() != VK_SUCCESS) {
        showError("Error initializing Volk");
        return false;
    }

    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, // used by vulkan internally to cast the struct to a generic type
        .pApplicationName = "Learning Vulkan: First Triangle",
        .apiVersion = VulkanVersion,
    };

    // find required extensions
    uint32_t instExtCount = 0;
    const char *const *extensions = SDL_Vulkan_GetInstanceExtensions(&instExtCount);

    std::vector<const char*> requestedExtensions{
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME
    };
    for (int i =0; i< instExtCount; ++i) {
        requestedExtensions.push_back(extensions[i]);
    }

    // enable the validation layer
    std::vector<const char*> requestedLayers{
        "VK_LAYER_KHRONOS_validation"
    };

    // DEBUG INFO
    VkDebugUtilsMessengerCreateInfoEXT debugInfo
    {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback
    };

    VkInstanceCreateInfo instCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = &debugInfo,    // Vulkan Structures into linked lists. Vulkan will follow each pNext pointer.
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(requestedLayers.size()),
        .ppEnabledLayerNames = requestedLayers.data(),

        // Pair fields COMMON STRUCTURE:

        // State number of things we wish to pass:
        .enabledExtensionCount = static_cast<uint32_t>(requestedExtensions.size()),
        // Field holding the memory we wish to pass:
        .ppEnabledExtensionNames = requestedExtensions.data()
    };
    if (vkCreateInstance(&instCreateInfo, nullptr,&vulkanInstance) != VK_SUCCESS) {
        return false;
    }
    volkLoadInstance(vulkanInstance);
    return true;
}

VKAPI_ATTR VkBool32 VKAPI_CALL Application::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData)
{
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        std::cerr << "Validation Layer:" << pCallbackData->pMessage << std::endl;
    }
    return VK_FALSE;
}
bool Application::initializeVulkan() {
    if (!createVulkanInstance()) {
        showError("Error creating Vulkan instance");
        return false;
    }
    if (!createSurface()) {
        showError("Error creating surface");
        return false;
    }
    if (physicalDevice = findPhysicalDevice(); !physicalDevice) {
        showError("Error finding physical device");
        return false;
    }
    if (!findGraphicsQueue()) {
        showError("Unable to find a compatible graphics queue");
        return false;
    }

    if (!createDevice(physicalDevice)) {
        showError("Could not create the logical GPU device");
        return false;
    }
}

// TODO: 25:30 createDeivce

// VkDevice is created via a VkPhysicalDevice
bool Application::createDevice(VkPhysicalDevice phyiscalDevice) {
    // query the supported features

    VkPhysicalDeviceVulkan14Features supportedFeatures14
    {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES, .pNext = nullptr};
    VkPhysicalDeviceVulkan13Features supportedFeatures13
    {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,.pNext = &supportedFeatures14};
    VkPhysicalDeviceVulkan12Features supportedFeatures12
    {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,.pNext = &supportedFeatures13};
    VkPhysicalDeviceFeatures2 supprotedFeatures
    {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &supportedFeatures12};
    vkGetPhysicalDeviceFeatures2(physicalDevice, &supprotedFeatures);

    if (!supportedFeatures13.dynamicRendering || !supportedFeatures13.synchronization2 ||
        !supportedFeatures12.timelineSemaphore)
    {
        showError("Physcical device doesn't meet the feature requirements");
        return false;
    }

    // Produce another chain of features structure
    // Seperate Features struct chain for device creation

    VkPhysicalDeviceVulkan14Features features14
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
        .pNext = nullptr,
    };
    VkPhysicalDeviceVulkan13Features features13
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &features14,
        .synchronization2 = VK_TRUE, // Modernized synchronization
        .dynamicRendering = VK_TRUE, // no renderpasses or subpasses
    };
    VkPhysicalDeviceVulkan12Features features12
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
        .pNext = &features13,
        .timelineSemaphore = VK_TRUE
    };
    VkPhysicalDeviceFeatures2 features // streamlined multi-frame-in-flight handling
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &features12
    };

    // Request the queues we'll be using
    // We need a queue to submit work to the GPU
    std::vector<float> queuePriorities{1.0f};
    VkDeviceQueueCreateInfo gfxQueueInfo
    {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = gfxQueueFamIdx,
        .queueCount = 1,
        .pQueuePriorities = queuePriorities.data()
    };

    // Device specific extensions

    const std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkDeviceCreateInfo devCreateInfo
    {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &gfxQueueInfo,
        .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
        .ppEnabledExtensionNames = deviceExtensions.data(),
        .pEnabledFeatures = nullptr // features struct chain is set in pNext
        // passing the chain using pEnabledFeatures in modern vulkan will
        // result in a compile error as the types wont match
    };

    if (vkCreateDevice(physicalDevice, &devCreateInfo,nullptr,&device) != VK_SUCCESS) {
        return false;
    }

    // Grab the VkQueue object family
    vkGetDeviceQueue(device, gfxQueueFamIdx,0,&gfxQueue);
    if (!gfxQueue) {
        showError("Could not get the graphics queue");
        return false;
    }
    return true;
}
bool Application::findGraphicsQueue() {
    // grab all queue families

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties2> queueFamProps(queueFamilyCount,{.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2});
    vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queueFamilyCount, queueFamProps.data());

    for (int currentFamilyIdx = 0; currentFamilyIdx < queueFamProps.size(); currentFamilyIdx++) {
        // ensure it has presentation support
        VkBool32 hasPresentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice,currentFamilyIdx,surface, &hasPresentSupport);

        const auto &props = queueFamProps[currentFamilyIdx];

        // ensure this is a GRAPHICS queue with presentation support
        if (props.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT && hasPresentSupport) {
            gfxQueueFamIdx = currentFamilyIdx;
            return true;
        }
    }
    return false;
}

VkPhysicalDevice Application::findPhysicalDevice() {
    // enumerate all physical devices
    uint32_t physDeviceCount = 0;

    // Notice: We are calling vkEnumeratePhysicalDevices twice

    // Passing a nullPointer as the buffer address: Vulkan only gives us the count of the items
    vkEnumeratePhysicalDevices(vulkanInstance, &physDeviceCount, nullptr);
    // Allocate a buffer to hold that ammount of items
    std::vector<VkPhysicalDevice> physicalDevices(physDeviceCount);
    // Call the function again to write to the newly created buffer
    vkEnumeratePhysicalDevices(vulkanInstance, &physDeviceCount, physicalDevices.data());

    VkPhysicalDevice physicalDevice = nullptr;
    if (physDeviceCount) {
        physicalDevice = physicalDevices[0]; // default to first GPU

        for (auto &pDev : physicalDevices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(pDev, &props);

            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                std:: cout << "Found Physical Device " << props.deviceName << std::endl;
                physicalDevice = pDev;
                break;
            }
        }
    }
    return physicalDevice;
}
bool Application::createSurface() {
    if (!SDL_Vulkan_CreateSurface(window, vulkanInstance, nullptr, &surface)) {
        return false;
    }
    return true;
}

void Application::render() {
    while (running) {

    }
}
void Application::shutdown() {
    // Some other cleanup
    std::cout << "Cleaning Up and shutting down" << std::endl;
    if (vulkanInstance) {
        vkDestroyInstance(vulkanInstance, nullptr);
    }
    volkFinalize();

    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
}
