#include "application.h"

#include <iostream>
#include <ostream>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#define VOLK_IMPLEMENTATION
#include <volk.h>
#define VMA_IMPLEMENTATION
#include <fstream>
#include <sstream>
#include <vk_mem_alloc.h>
#include <shaderc/shaderc.hpp>
#include "external/tiny_gltf_v3.h"
#include "external/stb_image.h"
#include "structs.h"



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

bool Application::loadData() {
    constexpr size_t vertexBufferBytes = 64* 1024* 1024; // 64MB vertex budget
    constexpr size_t indexBufferBytes  = 32* 1024* 1024; // 32MB vertex budget
    constexpr size_t totalVerts  = vertexBufferBytes / sizeof(Vertex);
    constexpr size_t totalIndices = indexBufferBytes / sizeof(uint32_t);

    m_vertices.resize(totalVerts);
    m_indices.resize(totalIndices);

    uint32_t purplePixelData = 0xFF00FF;
    Image purplePixeel
    {
      .width = 1,
        .height = 1,
        .channels = 4,
        .data = reinterpret_cast<unsigned char*>(&purplePixelData),
    };

    VkCommandBuffer debugImgCmdBuff = startTransientCommandBuffer();
}

VkCommandBuffer Application::startTransientCommandBuffer() {

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

    // Use the VMA (Vulkan Memory Allocator) library to avoid PAIN
    if (!initializeVMA()) {
        showError("Unable to create Vulkan Memory Allocator");
        return false;
    }

    // Swapchain
    if (!createSwapchain(width,height)) {
        showError("Unable to create swapchain");
        return false;
    }

    // Shaders and Graphics Pipeline

    //Shaders - compiled in runtime using shaderc
    if (!createShaders()) {
        showError("Error creating shader modules");
        return false;
    }

    if (pipeline = createGraphicsPipeline(); !pipeline) {
        showError("Unable to initialize graphics pipeline");
        return false;
    }

    if (!createSyncResources()) {
        showError("Error: Could not create the sync related resources");
        return false;
    }
    if (!createCommandBuffers()) {
        showError("Could not create command buffer objects");
        return false;
    }
    return true;
}


bool Application::createCommandBuffers() {

    // Iterate over Frames in Flight and create a command pool and command buffer for each one
    for (FrameResources &res : frameResources) {
        VkCommandPoolCreateInfo poolInfo
        {
          .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .queueFamilyIndex = gfxQueueFamIdx
        };

        if (vkCreateCommandPool(device, &poolInfo, nullptr, &res.commandPool) != VK_SUCCESS) {
            showError("Unable to create command buffer pool");
            return false;
        }

        // create command buffer for this frame;

        VkCommandBufferAllocateInfo cmdAllocInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = res.commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        if (vkAllocateCommandBuffers(device,&cmdAllocInfo,&res.commandBuffer) != VK_SUCCESS) {
            showError("Unable to allocate command buffer");
            return false;
        }
    }
    return true;
}

// Frames in flight:
// Multiple copies of our data
bool Application::createSyncResources() {

    // Timeline semaphore represents a monotonically increasing 64 bit integer. It never decreases
    // CPU can wait for a timeline value set
    VkSemaphoreTypeCreateInfo semaphoreTypeInfo
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = MaxFramesInFlight
    };
    VkSemaphoreCreateInfo semaphoreInfo
    {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &semaphoreTypeInfo
    };
    if (vkCreateSemaphore(device,&semaphoreInfo,nullptr,&timelineSemaphore) != VK_SUCCESS) {
        showError("Failed to create timeline semaphore");
        return false;
    }

    // per frame image-require semaphore.
    for (FrameResources &res : frameResources) {
        VkSemaphoreCreateInfo semaphoreInfo {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (vkCreateSemaphore(device,&semaphoreInfo,nullptr,&res.imageAcquiredSemaphore) != VK_SUCCESS) {
            showError("Failed to create per-frame image acquired semaphore");
            return false;
        }
    }
    return true;
}

// Configuration about how we are rendering pixels to the frame buffer
// Returns a VkPipeline Handle
// TODO: In the future: Different paramaters to get back VkPipelines representing different materials
VkPipeline Application::createGraphicsPipeline() {

    // Two parts: Layout object and the pipeline
    // ## Pipeline Layout : similar to a Function Signature ##
    //      - Descriptors and Constants

    // ## Pipeline (Material Type) : Body of the function
    //      - Vertex Data format
    //      - Shaders
    //      - Depth/Stencil
    //      - Blending
    //      - Rasterization
    //      - etc.

    // Common : Single Pipeline Layout with various pipelines for different materials

    VkPipelineLayoutCreateInfo pipelineLayoutInfo
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 0,        // This lets vulkan know that this pipeline layout will not
        .pushConstantRangeCount = 0 // be associated with any descriptor sets or push constants (used to get data into our shaders)
                                    // TODO/NOTE: THIS WILL CHANGE
    };

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        showError("Failed to create the vk pipeline layout");
        return nullptr;
    };

    //Configure shader stages

    //Entry Point function. In this case each shader has a main function
    const char *entryPoint = "main";
    //TODO/NOTE: It is possible to use a single VkShaderModule for multiple stages by using a different entrypoint
    //              function for the vert and fragment shader stages!


    // Associate shader module handles with shader stages of graphics pipeline
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages
    {
     {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vertexShaderModule,
         .pName = entryPoint
        },
     {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = fragmentShaderModule,
         .pName = entryPoint
        }
    };

    //TODO: Create and use Buffer for vertices representing mesh data
    //      Ignore for now as vertex data is hard coded for the first triangle

    //vertex pulling. Dont define vertex input details

    VkPipelineVertexInputStateCreateInfo vertInputInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };

    // Input Assembly, pipeline set to expect triangle list topology
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };

    // Depth/Stencil config
    VkPipelineDepthStencilStateCreateInfo depthStencilInfo
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .stencilTestEnable = VK_FALSE
    };

    // Dynamic Rendering will set this up dynamically but we need this struct
    VkPipelineViewportStateCreateInfo viewportInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = nullptr,
        .scissorCount = 1,
        .pScissors = nullptr
    };

    //Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterInfo
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };

    //Multisampling
    VkPipelineMultisampleStateCreateInfo multisampleInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT // Disable Multisampling
    };

    //Alpha bnlending
    //Disabled for now but this attachment info and write mask are still needed.
    VkPipelineColorBlendAttachmentState ColorBlendAttachState
    {
      .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    VkPipelineColorBlendStateCreateInfo blendinfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &ColorBlendAttachState
    };

    // Enable Dynamic State
    std::vector<VkDynamicState> dynamicState
    {
      VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicStateInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicState.size()),
        .pDynamicStates = dynamicState.data()
    };

    // New Vulkan Dynamic Rendering Feature
    // This structure is required for dynamic rendering
    VkPipelineRenderingCreateInfo renderInfo
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &swapchainFormat,
        .depthAttachmentFormat = depthFormat
    };

    // FINALLY : Create the graphics pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo
    {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderInfo,
        .stageCount = static_cast<uint32_t>(shaderStages.size()),
        .pStages = shaderStages.data(),
        .pVertexInputState = &vertInputInfo,
        .pInputAssemblyState = &inputAssemblyInfo,
        .pViewportState = &viewportInfo,
        .pRasterizationState = &rasterInfo,
        .pMultisampleState = &multisampleInfo,
        .pDepthStencilState = &depthStencilInfo,
        .pColorBlendState = &blendinfo,
        .pDynamicState = &dynamicStateInfo,
        .layout = pipelineLayout,
        .renderPass = VK_NULL_HANDLE,
    };

    VkPipeline newPipeline;

    if (vkCreateGraphicsPipelines(device,nullptr,1,&pipelineInfo,nullptr,&newPipeline) != VK_SUCCESS) {
        showError("Failed to create the graphics pipeline");
        return nullptr;
    }
    return newPipeline;

}

std::string readTextFile(const std::string &filePath) {
    std::ifstream infile(filePath);
    if (infile.is_open()) {
        std::stringstream buff;
        buff << infile.rdbuf();
        const std::string output = buff.str();
        infile.close();
        return output;
    }
    return std::string();
}

VkShaderModule Application::createShaderModule(const std::string &fileName,
                                                shaderc_shader_kind kind) const
{
    // Read shader file
    const std::string shaderPath = SHADER_DIR + fileName;
    const std::string src = readTextFile(shaderPath);
    if (src.empty()) {
        showError("Shader File does not exist: " + shaderPath);
        return nullptr;
    }

    // compile shader to SPIR-V
    std::cout << "Compiling Shader: " << shaderPath << std::endl;
    shaderc::Compiler compiler;
    shaderc::CompileOptions opts;

    opts.SetTargetEnvironment(shaderc_target_env_vulkan,shaderc_env_version_vulkan_1_4);
    opts.SetTargetSpirv(shaderc_spirv_version_1_6);
    opts.SetOptimizationLevel(shaderc_optimization_level_performance);
    // If compiled: we get an object of type CompilationResult containing a buffer of unsigned integers representing
    // the compiled SPIR-V binary data
    shaderc::CompilationResult result = compiler.CompileGlslToSpv(src,kind,fileName.c_str(),opts);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        std::cerr << "Shader Compilation Error: " << result.GetErrorMessage() << std::endl;
        return nullptr;
    }

    // VKShaderModule will be the handle to the compiled Vulkan Shader
    const size_t shaderSize = (result.end() - result.cbegin()) * sizeof(u_int32_t);
    // Pass SPIR-V to vulkan and create shader module
    VkShaderModuleCreateInfo shaderModuleCreateInfo
    {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shaderSize, // size in bytes
        .pCode = result.cbegin() // pointer to the start of the buffer
    };

    VkShaderModule shaderModule = nullptr;
    if (vkCreateShaderModule(device,&shaderModuleCreateInfo,nullptr,&shaderModule) != VK_SUCCESS) {
        showError("Failed to create shader module");
        return nullptr;
    }
    return shaderModule; // Return handle to vkShaderModule to use in our pipeline
}


bool Application::createShaders() {
    //create shader modules
    if (vertexShaderModule = createShaderModule("shader.vert",shaderc_vertex_shader); !vertexShaderModule) {
        return false;
    }
    if (fragmentShaderModule = createShaderModule("shader.frag",shaderc_fragment_shader); !fragmentShaderModule) {
        return false;
    }
    return true;
}


bool Application::createSwapchain(uint32_t width, uint32_t height) {
    // track swapchain size seperate from window size

    swapchainWidth = width;
    swapchainHeight = height;

    // ensure we request an appropriate number of images
    VkSurfaceCapabilitiesKHR surfaceCaps{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice,surface,&surfaceCaps) != VK_SUCCESS) {
        showError("Failed to get surface capabilities");
        return false;
    }

    uint32_t requestedImageCount = std::max(2u,surfaceCaps.minImageCount);
    if (surfaceCaps.maxImageCount > 0) {
        requestedImageCount = std::min(requestedImageCount,surfaceCaps.maxImageCount);
    }

    VkSwapchainCreateInfoKHR swapchainCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = requestedImageCount,
        .imageFormat = swapchainFormat,
        .imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = {.width =  swapchainWidth,.height = swapchainHeight},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = surfaceCaps.currentTransform, // tell os how to orient the image - most cases identity matrix
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR // First in First Out - standard guarantees this. Works well with vsync
        //.presentMode = VK_PRESENT_MODE_MAILBOX_KHR
        //.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR //vsync off mode
    };

    if (vkCreateSwapchainKHR(device,&swapchainCreateInfo,nullptr,&swapchain) != VK_SUCCESS) {
        showError("Error creating swapchain");
        return false;
    }

    // ask for the swapchain images
    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(device,swapchain,&imageCount,nullptr);
    swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device,swapchain,&imageCount,swapchainImages.data());
    swapchainImageViews.resize(imageCount);

    // now create swapchain image views
    for (size_t i = 0; i < swapchainImages.size(); ++i) {
        VkImageViewCreateInfo imgViewInfo
        {
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapchainImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchainFormat,
            .subresourceRange
            {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .baseMipLevel = 0,
              .levelCount = 1,
              .baseArrayLayer = 0,
              .layerCount = 1
            }
        };
        if (vkCreateImageView(device,&imgViewInfo,nullptr,&swapchainImageViews[i]) != VK_SUCCESS) {
            showError("Error creating swapchain image view");
            return false;
        }
    }

    // Semaphores signaling render completion and that the image is ready for display

    // Create a semaphore corresponding to each swapchain image
    renderCompleteSemaphores.resize(swapchainImages.size());
    for (VkSemaphore &semaphore : renderCompleteSemaphores) {
        VkSemaphoreCreateInfo semInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

        if (vkCreateSemaphore(device,&semInfo,nullptr,&semaphore) != VK_SUCCESS) {
            showError("Error creating the ''render-complete'' semaphore");
            return false;
        }
    }

    //create depth-Buffer
    VkImageCreateInfo depthCreateInfo
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = depthFormat,  // Set at a diffent place
        .extent {.width = swapchainWidth,.height = swapchainHeight,.depth = 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL, // Memory arangement. TILING_OPTIMAL is a memory arrangement optimal for smapling
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, // Image can be used during depth or stencil operations
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    // Allocate memory for the image using VMA
    VmaAllocationCreateInfo allocInfo
    {
      .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
    };
    if (vmaCreateImage(vmaAllocator,&depthCreateInfo,&allocInfo,&depthImage,&depthImageAllocation,nullptr) != VK_SUCCESS)
    {
        showError("Error creating depth buffer image");
        return false;
    }

    //Create a view into the image memory that will let us use it as a depth attachment layer
    VkImageViewCreateInfo depthImgViewInfo
    {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = depthImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = depthFormat, // defined in application.h
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,.levelCount = 1,.layerCount = 1}
    };

    // Create the Image View for the depth buffer
    if (vkCreateImageView(device,&depthImgViewInfo,nullptr,&depthImageView) != VK_SUCCESS) {
        showError("Error creating depth image view");
        return false;
    }
    return true;
}

bool Application::initializeVMA() {

    VmaVulkanFunctions vmaFuncInfo{};

    // Provide VMA some info about our setup
    VmaAllocatorCreateInfo vmaAllocInfo
    {
        // We intend to use the buffer device address features of modern vulkan
        // This will allow us to access vram directly using shaders
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = physicalDevice,
        .device = device,
        .pVulkanFunctions = &vmaFuncInfo,
        .instance = vulkanInstance,
        .vulkanApiVersion = VulkanVersion
    };

    // import directly from volk
    vmaImportVulkanFunctionsFromVolk(&vmaAllocInfo,&vmaFuncInfo);

    if (vmaCreateAllocator(&vmaAllocInfo,&vmaAllocator) != VK_SUCCESS) {
        return false;
    }
    return true;
}
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

    if ( !supportedFeatures13.dynamicRendering || !supportedFeatures13.synchronization2 ||
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
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &features13,
        .timelineSemaphore = VK_TRUE
    };
    VkPhysicalDeviceFeatures2 features // streamlined multi-frame-in-flight handling
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
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

    // Ensure the selected physical device and surface combination can support the swapchain color format
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount,surfaceFormats.data());

    bool formatSupported = false;
    for (const VkSurfaceFormatKHR &surfFormat : surfaceFormats) {
        if (surfFormat.format == swapchainFormat) {
            formatSupported = true;
            break;
        }
    }
    if (!formatSupported) {
        showError("Requested swapchain format is not supported by the surface");
        return nullptr;
    }

    // If GPU and surface combo cant support the format - check which physical device is being chosen
    // and review the vector of supported formats that vulkan returns
    return physicalDevice;
}
bool Application::createSurface() {
    if (!SDL_Vulkan_CreateSurface(window, vulkanInstance, nullptr, &surface)) {
        return false;
    }
    return true;
}

void Application::render() {
    // Check if swapchain is still valid
    if (requireSwapchainRecreate) {
        vkDeviceWaitIdle(device);
        destroySwapchain();
        createSwapchain(width,height);
        requireSwapchainRecreate = false;
    }

    //TimelineSemaphore sync
    const uint32_t frameResIndex = frameIndex++ % MaxFramesInFlight;
    const uint64_t signalValue = nextSignalValue++;
    const uint64_t waitValue = signalValue - MaxFramesInFlight;
    // MaxFramesInFlight = 2
    // Timeline Init = 2 (MaxFramesInFlight)
    // nextSignalValue = MaxFramesInFlight + 1

    //Frame 1 : Signal = 3, Wait = 1
    //Frame 2 : Signal = 4, Wait = 2
    //Frame 3 : Signal = 5, Wait = 3
    //Frame 4 : Signal = 6, Wait = 4

    VkSemaphoreWaitInfo waitInfo
    {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &timelineSemaphore,
        .pValues = &waitValue
    };

    vkWaitSemaphores(device,&waitInfo,UINT64_MAX);

    // Now we can start recording commands as it is safe

    //Grab current  frame resourcves obj and reset the command pool to record a fresh set of commands for this frame
    FrameResources &res = frameResources[frameResIndex];
    vkResetCommandPool(device,res.commandPool,0);

    // Get resources for this frame. We need the binary image acquired semaphore
    VkSemaphore imageAcquiredSemaphore = frameResources[frameResIndex].imageAcquiredSemaphore;

    uint32_t imageIndex = 0;
                                // device, swapchain, timeout val, binary semaphore to signal when image is ready to be drawn on
    VkResult acquireResult = vkAcquireNextImageKHR(device,swapchain,UINT64_MAX,
                                                    imageAcquiredSemaphore,VK_NULL_HANDLE,&imageIndex);

    //handle resize and out of date images - maybe swapchain recreate
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        requireSwapchainRecreate = true;
        return;
    }

    //Swapchain still usable but we should recreate it asap to avoid performance/graphical problems
    //Example: Resizing window
    else if ( acquireResult == VK_SUBOPTIMAL_KHR) {
        // can render this frame, recreate next time
        requireSwapchainRecreate = true;
    }

    // begin rendering commands here
    VkCommandBufferBeginInfo cmdBeginInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(res.commandBuffer,&cmdBeginInfo);

    std::vector<VkImageMemoryBarrier2> layoutBarriers
    {
      {
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
          .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
          .srcAccessMask = 0,
          .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
          .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          .image = swapchainImages[imageIndex],
          .subresourceRange
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .baseMipLevel = 0,
              .levelCount = 1,
              .baseArrayLayer = 0,
              .layerCount = 1
          }
      },
   {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .image = depthImage,
        .subresourceRange
        {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        }
      }
    };

    VkDependencyInfo depInfo
    {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = static_cast<uint32_t>(layoutBarriers.size()),
      .pImageMemoryBarriers = layoutBarriers.data(),
    };

    vkCmdPipelineBarrier2(res.commandBuffer,&depInfo);

    // setup color and depth attachments and begin rendering (Dynamic)
    VkRenderingAttachmentInfo colorAttachInfo
    {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchainImageViews[imageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,  // clear
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE, // keep data for presentation
        .clearValue{.color{0.01f,0.01f,0.01f,1}}
    };

    VkRenderingAttachmentInfo depthAttachInfo
    {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depthImageView,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue{.depthStencil{1.0f,0}}
    };
    VkRenderingInfo renderingInfo
    {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea
      {
          .offset{.x = 0, .y = 0},
          .extent = {.width = swapchainWidth, .height = swapchainHeight}
      },
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &colorAttachInfo,
      .pDepthAttachment = &depthAttachInfo,
    };

    // begin dynamic rendering

    vkCmdBeginRendering(res.commandBuffer,&renderingInfo);
    {
        VkViewport viewport
        {
            .x = 0, .y = 0,
            .width = static_cast<float>(swapchainWidth),
            .height = static_cast<float>(swapchainHeight),
        };
        vkCmdSetViewport(res.commandBuffer,0,1,&viewport);

        VkRect2D scissor
        {
          .offset{.x = 0, .y = 0},
            .extent{.width = swapchainWidth, .height = swapchainHeight}
        };

        vkCmdSetScissor(res.commandBuffer,0,1,&scissor);

        // draw the TRIANGLE!
        vkCmdBindPipeline(res.commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        vkCmdDraw(res.commandBuffer,3,1,0,0);
    }

    //end dynamic rendering - we're done with drawing. The command buffer contains rendering commands
    vkCmdEndRendering(res.commandBuffer);

    //transition the image from color attachment to presentation barrier so we can actually draw it
    VkImageMemoryBarrier2 presentatLayourBattier
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image = swapchainImages[imageIndex],
        .subresourceRange
        {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        }
    };

    VkDependencyInfo presentDepInfo
    {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &presentatLayourBattier,
    };
    vkCmdPipelineBarrier2(res.commandBuffer,&presentDepInfo);
    vkEndCommandBuffer(res.commandBuffer);

    VkSemaphoreSubmitInfo imageAcquireWaitInfo
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = imageAcquiredSemaphore,
        .stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // wait on this before drawing to image
    };

    //signal that the image can be presented
    std::vector<VkSemaphoreSubmitInfo> semaphoreSignals
    {
        { // render work completion signal here
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = renderCompleteSemaphores[imageIndex],
            .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT
        } ,
        { // Timeline Semaphore (Entire frame is completed)
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = timelineSemaphore,
            .value = signalValue,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
        }
    };

    // Submit command buffer to the queue
    VkCommandBufferSubmitInfo cmdSubmitInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = res.commandBuffer,
    };
    VkSubmitInfo2 submitInfo
    {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &imageAcquireWaitInfo, // ensure image is ready
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmdSubmitInfo,
        .signalSemaphoreInfoCount = static_cast<uint32_t>(semaphoreSignals.size()),
        .pSignalSemaphoreInfos = semaphoreSignals.data(),
    };
    vkQueueSubmit2(gfxQueue,1,&submitInfo,VK_NULL_HANDLE);

    // Finally, present the image:
    VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderCompleteSemaphores[imageIndex],
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &imageIndex,
        .pResults = nullptr
    };

    vkQueuePresentKHR(gfxQueue,&presentInfo);
}
void Application::shutdown() {
    std::cout << "Cleaning Up and shutting down" << std::endl;

    //Flush GPU
    vkDeviceWaitIdle(device);

    // frame and sync object cleanup
    if (timelineSemaphore) {
        vkDestroySemaphore(device, timelineSemaphore, nullptr);
    }
    for (auto &res : frameResources) {
        vkDestroySemaphore(device, res.imageAcquiredSemaphore, nullptr);
        vkDestroyCommandPool(device, res.commandPool, nullptr);
    }
    //pipeline cleanup
    if (pipelineLayout) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    }
    if (pipeline) {
        vkDestroyPipeline(device, pipeline, nullptr);
    }

    //cleanup shaders
    if (vertexShaderModule) {
        vkDestroyShaderModule(device, vertexShaderModule, nullptr);
    }
    if (fragmentShaderModule) {
        vkDestroyShaderModule(device, fragmentShaderModule, nullptr);
    }

    //destroy swapchain
    destroySwapchain();

    // VMA
    if (vmaAllocator) {
        vmaDestroyAllocator(vmaAllocator);
    }

    // cleanup Vulkan
    if (surface) {
        vkDestroySurfaceKHR(vulkanInstance, surface, nullptr);
    }
    if (device) {
        vkDestroyDevice(device, nullptr);
    }
    if (vulkanInstance) {
        vkDestroyInstance(vulkanInstance, nullptr);
    }
    volkFinalize();

    // SDL
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
}
void Application::destroySwapchain() {
    for (VkImageView swapchainImageView : swapchainImageViews) {
        vkDestroyImageView(device, swapchainImageView, nullptr);
    }
    swapchainImageViews.clear();

    // destroy semaphores
    for (VkSemaphore &semaphore : renderCompleteSemaphores) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    renderCompleteSemaphores.clear();

    if (swapchain) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = nullptr;
    }

    // Destroy Depth Buffer
    if (depthImageView) {
        vkDestroyImageView(device, depthImageView, nullptr);
        vmaDestroyImage(vmaAllocator,depthImage,depthImageAllocation);
        depthImageView = nullptr;
    }
}
