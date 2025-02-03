/*
*  Includes and helpful utilities
*/

#include <windows.h>
#include <vulkan\vulkan.h>
#include <vulkan\vulkan_win32.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t s32;

typedef float f32;

#define array_count(array) (sizeof(array) / sizeof((array)[0]))

/*
*  VulkanContext struct
*/

typedef struct
{
    HWND window;
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    u32 graphicsAndPresentQueueFamily;
    VkQueue graphicsAndPresentQueue;
    VkSwapchainKHR swapchain;
    VkFormat swapchainImageFormat;
    VkImage swapchainImages[2];
    VkImageView swapchainImageViews[2];
    VkExtent2D swapchainExtents;
    
} VulkanContext;

/*
*  File loading utility
*/

typedef struct
{
    void *data;
    size_t size;
    
} LoadedFile;

LoadedFile
load_entire_file(char *fileName)
{
    LoadedFile result = {NULL};
    
    FILE *handle;
    fopen_s(&handle, fileName, "rb");
    assert(handle);
    
    fseek(handle, 0, SEEK_END);
    result.size = ftell(handle);
    fseek(handle, 0, SEEK_SET);
    
    assert(result.size > 0);
    
    result.data = malloc(result.size);
    assert(result.data);
    
    size_t bytesRead = fread(result.data, 1, result.size, handle);
    assert(bytesRead == result.size);
    
    fclose(handle);
    
    return result;
}

/*
*  globalRunning and WindowProc
*/

static bool globalRunning;

LRESULT CALLBACK
vulkan_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
        case WM_CREATE:
        {
            OutputDebugString("Window created\n");
        } break;
        
        case WM_SIZE: 
        {
            OutputDebugString("Window resized\n");
        } break;
        
        case WM_CLOSE:
        case WM_DESTROY:
        {
            globalRunning = false;
        } break;
        
        default:
        {
            return DefWindowProc(window, message, wparam, lparam);
        } break;
    }
    
    return 0;
}

/*
*  Vulkan Validation layer's Debug Callback
*/

static VKAPI_ATTR VkBool32 VKAPI_CALL
vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                      VkDebugUtilsMessageTypeFlagsEXT messageType,
                      const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
                      void *userData)
{
    char buffer[4096] = {0};
    sprintf_s(buffer, sizeof(buffer), "Vulkan Validation layer: %s\n",
              callbackData->pMessage);
    OutputDebugString(buffer);
    
    return VK_FALSE;
}

/*
*  Vulkan Initialization Function
*/

VulkanContext
win32_init_vulkan(HINSTANCE instance, s32 windowX, s32 windowY, u32 windowWidth,
                  u32 windowHeight, char *windowTitle)
{
    VulkanContext vk = {NULL};
    
    /*
    *  Create window
    */
    
    // Register window class
    WNDCLASSEX winClass =
    {
        sizeof(WNDCLASSEX),
        0, // style
        vulkan_window_proc, // window procedure
        0, // cbClsExtra
        0, // cbWndExtra
        instance, // hInstance
        NULL, // hIcon
        NULL, // hCursor
        NULL, // hbrBackground
        NULL, // lpszMenuName
        "MyUniqueVulkanWindowClassName",
        NULL, // hIconSm
    };
    
    if (!RegisterClassEx(&winClass))
    {
        assert(!"Failed to register window class");
    }
    
    // Make sure the window is not resizable for simplicity
    DWORD windowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    
    RECT windowRect =
    {
        windowX, // left
        windowY, // top
        windowX + windowWidth, // right
        windowY + windowHeight, // bottom
    };
    
    AdjustWindowRect(&windowRect, windowStyle, 0);
    
    windowWidth = windowRect.right - windowRect.left;
    windowHeight = windowRect.bottom - windowRect.top;
    windowX = windowRect.left;
    windowY = windowRect.top;
    
    // Create window
    vk.window = CreateWindowEx(0, // Extended style
                               winClass.lpszClassName,
                               windowTitle,
                               windowStyle,
                               windowX, windowY, windowWidth, windowHeight,
                               NULL, NULL, instance, NULL);
    
    if (!vk.window)
    {
        assert(!"Failed to create window");
    }
    
    ShowWindow(vk.window, SW_SHOW);
    
    /*
    *  Set up enabled layers and extensions
    */
    
    // Query available instance layers
    u32 propertyCount = 0;
    vkEnumerateInstanceLayerProperties(&propertyCount, NULL);
    assert(propertyCount <= 32); // Ensure we don't exceed our fixed-size array
    
    VkLayerProperties layerProperties[32];
    vkEnumerateInstanceLayerProperties(&propertyCount, layerProperties);
    
    char *validationLayerName = "VK_LAYER_KHRONOS_validation";
    
    // Check if the requested validation layer is available
    bool validationLayerFound = false;
    for (u32 i = 0; i < propertyCount; i++)
    {
        if (strcmp(validationLayerName, layerProperties[i].layerName) == 0)
        {
            validationLayerFound = true;
            break;
        }
    }
    
    assert(validationLayerFound && "Validation layer not found!");
    char *enabledLayers[] = { validationLayerName };
    
    char *extensions[] =
    {
        // These defines are used instead of raw strings for future compatibility
        VK_KHR_SURFACE_EXTENSION_NAME, // "VK_KHR_surface"
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME, // "VK_KHR_win32_surface"
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME // "VK_EXT_debug_utils"
    };
    
    /*
    *  Create Vulkan Instance
    */
    
    /* This struct is technically optional, but it's worth adding for a nicer
       display when inspecting the app with tools like RenderDoc. */
    VkApplicationInfo appInfo =
    {
        VK_STRUCTURE_TYPE_APPLICATION_INFO,
        NULL,
        "My Clever App Name",
        1, // application Version
        "My Even Cleverer Engine Name",
        1, // engine Version
        VK_API_VERSION_1_3
    };
    
    /* This struct is necessary. The main purpose of this is to inform the
       Vulkan driver about which layers and extensions to load when calling
       vkCreateInstance. */
    VkInstanceCreateInfo createInfo =
    {
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        NULL,
        0, // flags (this is the only time I'm commenting on this)
        &appInfo,
        array_count(enabledLayers), // layer count
        enabledLayers, // layers to enable
        array_count(extensions), // extension count
        extensions // extension names
    };
    
    if (vkCreateInstance(&createInfo, NULL,
                         &vk.instance) != VK_SUCCESS)
    {
        assert(!"Failed to create vulkan instance");
    }
    
    /*
    *  Set up debug callback
    */
    
    VkDebugUtilsMessageSeverityFlagsEXT messageSeverity = 
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    
    VkDebugUtilsMessageTypeFlagsEXT messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo =
    {
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        NULL,
        0,
        messageSeverity,
        messageType,
        vulkan_debug_callback,
        NULL // user data
    };
    
    // Load the debug utils extension function
    PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT =
    (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(vk.instance, "vkCreateDebugUtilsMessengerEXT");
    
    VkDebugUtilsMessengerEXT debugMessenger;
    if (vkCreateDebugUtilsMessengerEXT(vk.instance, &debugCreateInfo, NULL,
                                       &debugMessenger) != VK_SUCCESS)
    {
        assert(!"Failed to create debug messenger!");
    }
    
    /* 
    *  Create surface
    */
    
    VkWin32SurfaceCreateInfoKHR surfaceCreateInfo =
    {
        VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        NULL,
        0,
        instance, // HINSTANCE
        vk.window // HWND
    };
    
    if (vkCreateWin32SurfaceKHR(vk.instance, &surfaceCreateInfo, NULL,
                                &vk.surface) != VK_SUCCESS)
    {
        assert(!"Failed to create surface");
    }
    
    /*
    *  Pick a physical device and the graphicsAndPresent queue family
    */
    
    u32 deviceCount = 0;
    vkEnumeratePhysicalDevices(vk.instance, &deviceCount, NULL);
    assert(deviceCount <= 8); // Ensure there are no more than 8 devices
    
    VkPhysicalDevice devices[8] = {NULL};
    vkEnumeratePhysicalDevices(vk.instance, &deviceCount, devices);
    
    // Choose the first available device as a fallback
    vk.physicalDevice = devices[0];
    
    // Search for a dedicated GPU (discrete GPU)
    for (u32 i = 0; i < deviceCount; i++)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);
        
        // If the device is a dedicated (discrete) GPU, prefer it
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            // Choose it as the physical device and break the loop
            vk.physicalDevice = devices[i];
            break;
        }
    }
    assert(vk.physicalDevice); // Ensure a physical device has been selected
    
    // Query the queue family properties for the chosen physical device
    u32 queueFamilyPropertyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vk.physicalDevice,
                                             &queueFamilyPropertyCount, NULL);
    // Ensure there are no more than 3 queue families
    assert(queueFamilyPropertyCount <= 3); 
    
    VkQueueFamilyProperties queueFamilyProperties[3] = {0};
    vkGetPhysicalDeviceQueueFamilyProperties(vk.physicalDevice,
                                             &queueFamilyPropertyCount,
                                             queueFamilyProperties);
    
    // Assume first queue family supports Graphics and Present capabilities
    u32 queueFamilyIndex = 0;
    
    // Ensure the queue family supports graphics
    assert(queueFamilyProperties[queueFamilyIndex].queueFlags
           & VK_QUEUE_GRAPHICS_BIT);
    
    VkBool32 presentSupport = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(vk.physicalDevice, queueFamilyIndex,
                                         vk.surface,
                                         &presentSupport);
    assert(presentSupport); // Ensure present support is available
    
    // Store the queue family index that supports both graphics and present
    vk.graphicsAndPresentQueueFamily = queueFamilyIndex;
    
    /*
    *  Create logical device
    */
    
    f32 queuePriorities[] = { 1.0f };
    VkDeviceQueueCreateInfo queueCreateInfo =
    {
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        NULL,
        0,
        vk.graphicsAndPresentQueueFamily,
        array_count(queuePriorities),
        queuePriorities
    };
    
    VkDeviceQueueCreateInfo queueCreateInfos[] = {queueCreateInfo};
    
    // Enable required device extensions (swapchain)
    char *deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    
    VkDeviceCreateInfo deviceCreateInfo =
    {
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        NULL,
        0,
        array_count(queueCreateInfos),
        queueCreateInfos,
        0, // enabledLayerCount deprecated
        NULL, // ppEnabledLayerNames deprecated
        array_count(deviceExtensions),
        deviceExtensions,
        NULL // pEnabledFeatures
    };
    
    // Create the actual logical device finally
    if (vkCreateDevice(vk.physicalDevice, &deviceCreateInfo, NULL,
                       &vk.device) != VK_SUCCESS)
    {
        assert(!"Failed to create logical device");
    }
    
    /*
    *  Get graphicsAndPresentQueue from device
    */
    
    vkGetDeviceQueue(vk.device, vk.graphicsAndPresentQueueFamily, 0,
                     &vk.graphicsAndPresentQueue);
    assert(vk.graphicsAndPresentQueue);
    
    /*
    *  Create swapchain 
    */
    
    // Query surface capabilities
    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physicalDevice, vk.surface,
                                              &surfaceCapabilities);
    
    // Save the swapchain image format and extents
    vk.swapchainImageFormat = VK_FORMAT_B8G8R8A8_SRGB;
    vk.swapchainExtents = surfaceCapabilities.currentExtent;
    
    VkSwapchainCreateInfoKHR swapchainCreateInfo =
    {
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        NULL,
        0,
        vk.surface,
        array_count(vk.swapchainImages), // minImageCount (2)
        vk.swapchainImageFormat,
        VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, // imageColorSpace
        vk.swapchainExtents, // imageExtent
        1, // imageArrayLayers
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, // imageUsage
        VK_SHARING_MODE_EXCLUSIVE,
        0, // queueFamilyIndexCount
        NULL, // pQueueFamilyIndices
        surfaceCapabilities.currentTransform, // preTransform
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_PRESENT_MODE_FIFO_KHR,
        VK_TRUE, // clipped
        NULL // oldSwapchain
    };
    
    if (vkCreateSwapchainKHR(vk.device, &swapchainCreateInfo, NULL,
                             &vk.swapchain) != VK_SUCCESS)
    {
        assert(!"Failed to create the swapchain");
    }
    
    /*
    *  Get swapchain images and create their views
    */
    
    u32 imageCount = 0;
    vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &imageCount, NULL);
    assert(imageCount == array_count(vk.swapchainImages));
    
    vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &imageCount,
                            vk.swapchainImages);
    
    // For each swapchain image
    for (u32 i = 0; i < imageCount; i++)
    {
        assert(vk.swapchainImages[i]);
        
        VkComponentMapping swizzle =
        {
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY
        };
        
        VkImageSubresourceRange subRange =
        {
            VK_IMAGE_ASPECT_COLOR_BIT,
            0, // baseMipLevel
            1, // levelCount
            0, // baseArrayLayer
            1  // layerCount
        };
        
        VkImageViewCreateInfo viewInfo =
        {
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            NULL,
            0,
            vk.swapchainImages[i],
            VK_IMAGE_VIEW_TYPE_2D,
            vk.swapchainImageFormat,
            swizzle,
            subRange
        };
        
        // Create the image view
        vkCreateImageView(vk.device, &viewInfo, NULL,
                          &vk.swapchainImageViews[i]);
        
        assert(vk.swapchainImageViews[i]);
    }
    
    return vk;
}

/*
*  Create shader module function
*/

VkShaderModule
create_shader_module(VulkanContext *vk, void *code, size_t size)
{
    VkShaderModule result;
    
    VkShaderModuleCreateInfo createInfo =
    {
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        NULL,
        0,
        size,
        (u32 *)code
    };
    
    if (vkCreateShaderModule(vk->device, &createInfo, NULL,
                             &result) != VK_SUCCESS)
    {
        assert(!"Failed to create shader module!");
    }
    
    return result;
}

/*
*  WinMain application entry point
*/

int CALLBACK
WinMain(HINSTANCE instance, HINSTANCE prevInstance, LPSTR cmdLine, int showCmd)
{
    VulkanContext vk = win32_init_vulkan(instance, 100, 100, 800, 600,
                                         "My Shiny Vulkan Window");
    
    /*
    *  App-specific Vulkan objects
    */
    
    VkRenderPass renderPass;
    VkFramebuffer swapchainFramebuffers[2];
    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;
    VkFence frameFence;
    
    /*
    *  Create the Render Pass
    */
    
    // Describe the color attachment (the swapchain image)
    VkAttachmentDescription colorAttachment =
    {
        0, // flags
        vk.swapchainImageFormat,
        VK_SAMPLE_COUNT_1_BIT, // no multisampling
        VK_ATTACHMENT_LOAD_OP_CLEAR, // load operation (clear the screen)
        VK_ATTACHMENT_STORE_OP_STORE, // store op (save the result)
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, // stencil load op (ignored)
        VK_ATTACHMENT_STORE_OP_DONT_CARE, // stencil store op (ignored)
        VK_IMAGE_LAYOUT_UNDEFINED, // initial image layout
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR // final layout (optimal to present)
    };
    
    VkAttachmentDescription colorAttachments[] = { colorAttachment };
    
    VkAttachmentReference colorAttachmentRef =
    {
        0, // index of the attachment in the render pass
        // layout during rendering (optimal for rendering color data)
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };
    
    VkAttachmentReference colorAttachmentRefs[] = { colorAttachmentRef };
    
    // Describe the render subpass
    VkSubpassDescription subpass =
    {
        0, // flags
        VK_PIPELINE_BIND_POINT_GRAPHICS, // pipeline bind point
        0, // input attachment count (ignored)
        NULL, // input attachments (ignored)
        array_count(colorAttachmentRefs),
        colorAttachmentRefs,
        NULL, // resolve attachments (ignored)
        NULL, // depth stencil attachment (ignored)
        0, // preserve attachment count (ignored)
        NULL // preserve attachments (ignored)
    };
    
    VkSubpassDescription subpasses[] = { subpass };
    
    VkRenderPassCreateInfo renderPassInfo =
    {
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        NULL,
        0,
        array_count(colorAttachments),
        colorAttachments,
        array_count(subpasses),
        subpasses,
        0, // dependency count (ignored)
        NULL  // dependencies (ignored)
    };
    
    if (vkCreateRenderPass(vk.device, &renderPassInfo, NULL,
                           &renderPass) != VK_SUCCESS)
    {
        assert(!"Failed to create render pass");
    }
    
    /*
    *  Create Swapchain image's Framebuffers
    */
    
    for (u32 i = 0; i < array_count(vk.swapchainImageViews); i++)
    {
        VkImageView frameBufferAttachments[] = { vk.swapchainImageViews[i] };
        
        // Fill framebuffer create info
        VkFramebufferCreateInfo framebufferInfo =
        {
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            NULL,
            0,
            renderPass,
            array_count(frameBufferAttachments),
            frameBufferAttachments,
            vk.swapchainExtents.width,
            vk.swapchainExtents.height,
            1, // layers
        };
        
        // Create the framebuffer
        if (vkCreateFramebuffer(vk.device, &framebufferInfo, NULL,
                                &swapchainFramebuffers[i]) != VK_SUCCESS)
        {
            assert(!"Failed to create framebuffer");
        }
    }
    
    /*
    *  Create Semaphores
    */
    
    VkSemaphoreCreateInfo semaphoreInfo =
    {
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        NULL,
        0
    };
    
    vkCreateSemaphore(vk.device, &semaphoreInfo, NULL,
                      &imageAvailableSemaphore);
    
    vkCreateSemaphore(vk.device, &semaphoreInfo, NULL,
                      &renderFinishedSemaphore);
    
    /*
    *  Create Command Pool and Command Buffer
    */
    
    VkCommandPoolCreateInfo commandPoolCreateInfo =
    {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        NULL,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        vk.graphicsAndPresentQueueFamily
    };
    
    if (vkCreateCommandPool(vk.device, &commandPoolCreateInfo, NULL,
                            &commandPool) != VK_SUCCESS)
    {
        assert(!"Failed to create a command pool");
    }
    
    VkCommandBufferAllocateInfo allocInfo =
    {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        NULL,
        commandPool,
        VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        1 // commandBufferCount
    };
    
    vkAllocateCommandBuffers(vk.device, &allocInfo,
                             &commandBuffer);
    
    /*
    *  Load SPIR-V and Create Shader Modules
    */
    
    LoadedFile vertexShader = load_entire_file("../shaders/vert.spv");
    assert(vertexShader.size > 0);
    
    LoadedFile fragmentShader = load_entire_file("../shaders/frag.spv");
    assert(fragmentShader.size > 0);
    
    // Create shader modules from loaded binaries
    VkShaderModule vertShaderModule =
        create_shader_module(&vk, vertexShader.data, vertexShader.size);
    
    VkShaderModule fragShaderModule =
        create_shader_module(&vk, fragmentShader.data, fragmentShader.size);
    
    /*
    *  Define Shader Stage Create Info
    */
    
    VkPipelineShaderStageCreateInfo vertShaderStageInfo =
    {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        NULL,
        0,
        VK_SHADER_STAGE_VERTEX_BIT,
        vertShaderModule,
        "main", // entry point
        NULL // specialization info
    };
    
    VkPipelineShaderStageCreateInfo fragShaderStageInfo =
    {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        NULL,
        0,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        fragShaderModule,
        "main", // entry point
        NULL, // specialization info
    };
    
    VkPipelineShaderStageCreateInfo shaderStageInfo[] =
    {
        vertShaderStageInfo,
        fragShaderStageInfo
    };
    
    /*
    *  Define Vertex Input Create Info
    */
    
    VkPipelineVertexInputStateCreateInfo vertexInputStateInfo =
    {
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        NULL,
        0,
        0, NULL, 0, NULL // No vertex buffers
    };
    
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateInfo =
    {
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        NULL,
        0,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        VK_FALSE // primitiveRestartEnable
    };
    
    /*
    *  Define Dynamic State Crate Info
    */
    
    VkPipelineDynamicStateCreateInfo dynamicStateInfo =
    {
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        NULL,
        0,
        0, NULL // No dynamic states
    };
    
    /*
    *  Define Viewport State Create Info
    */
    
    VkViewport viewport =
    {
        0, 0, // x, y
        (f32)vk.swapchainExtents.width,
        (f32)vk.swapchainExtents.height,
        0, 0 // min, max depth
    };
    
    VkViewport viewports[] = { viewport };
    
    VkRect2D scissor =
    {
        {0, 0}, // offset
        vk.swapchainExtents
    };
    
    VkRect2D scissors[] = { scissor };
    
    VkPipelineViewportStateCreateInfo viewportStateInfo =
    {
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        NULL,
        0,
        array_count(viewports),
        viewports,
        array_count(scissors),
        scissors
    };
    
    /*
    *  Define Rasterization State Create Info
    */
    
    VkPipelineRasterizationStateCreateInfo rasterizationStateInfo =
    {
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        NULL,
        0,
        VK_FALSE, // depthClampEnable
        VK_FALSE, // rasterizerDiscardEnable
        VK_POLYGON_MODE_FILL, // polygonMode (solid triangles)
        VK_CULL_MODE_BACK_BIT, // cullMode
        VK_FRONT_FACE_CLOCKWISE, // frontFace
        VK_FALSE, 0, 0, 0, // no depth bias
        1.0f // lineWidth
    };
    
    /*
    *  Define Multisample State Create Info
    */
    
    VkPipelineMultisampleStateCreateInfo multisampleStateInfo =
    {
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        NULL,
        0,
        VK_SAMPLE_COUNT_1_BIT, // rasterizationSamples
        VK_FALSE, // sampleShadingEnable
        0, // minSampleShading
        NULL, // pSampleMask
        VK_FALSE, // alphaToCoverageEnable
        VK_FALSE, // alphaToOneEnable
    };
    
    /*
    *  Define Color Blend State Create Info
    */
    
    VkFlags colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    
    VkPipelineColorBlendAttachmentState colorBlendAttachment =
    {
        VK_FALSE, // blendEnable
        VK_BLEND_FACTOR_ZERO,
        VK_BLEND_FACTOR_ZERO,
        VK_BLEND_OP_ADD,
        VK_BLEND_FACTOR_ZERO,
        VK_BLEND_FACTOR_ZERO,
        VK_BLEND_OP_ADD,
        colorWriteMask,
    };
    
    VkPipelineColorBlendAttachmentState
        colorBlendAttachments[] = { colorBlendAttachment };
    
    VkPipelineColorBlendStateCreateInfo colorBlendStateInfo =
    {
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        NULL,
        0,
        VK_FALSE,
        VK_LOGIC_OP_CLEAR,
        array_count(colorBlendAttachments),
        colorBlendAttachments,
        {0, 0, 0, 0}
    };
    
    /*
    *  Create Pipeline Layout
    */
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo =
    {
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        NULL,
        0,
        0, NULL, // (no descriptor sets)
        0, NULL // (no push constant ranges)
    };
    
    if (vkCreatePipelineLayout(vk.device, &pipelineLayoutInfo, NULL,
                               &pipelineLayout) != VK_SUCCESS)
    {
        assert(!"Failed to create pipeline layout!");
    }
    
    /*
    *  Create Graphics Pipeline
    */
    
    VkGraphicsPipelineCreateInfo pipelineInfo =
    {
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        NULL,
        0,
        array_count(shaderStageInfo),
        shaderStageInfo,
        &vertexInputStateInfo,
        &inputAssemblyStateInfo,
        NULL, // pTessellationState
        &viewportStateInfo,
        &rasterizationStateInfo,
        &multisampleStateInfo,
        NULL, // pDepthStencilState
        &colorBlendStateInfo,
        &dynamicStateInfo,
        pipelineLayout,
        renderPass,
        0, // subpass index
        NULL, 0 // (no base pipeline)
    };
    
    // Create the graphics pipeline
    if (vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                  &pipelineInfo, NULL,
                                  &graphicsPipeline) != VK_SUCCESS)
    {
        assert(!"Failed to create graphics pipeline!");
    }
    
    /*
    *  Destroy Shader Modules and Create Frame Fence
    */
    
    vkDestroyShaderModule(vk.device, vertShaderModule, NULL);
    vkDestroyShaderModule(vk.device, fragShaderModule, NULL);
    
    VkFenceCreateInfo fenceInfo =
    {
        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        NULL,
        0
    };
    
    vkCreateFence(vk.device, &fenceInfo, NULL,
                  &frameFence);
    
    /*
    *  Main Loop
    */
    
    globalRunning = true;
    while (globalRunning)
    {
        /*
        *  Wait for the Frame Fence, then Reset it
        */
        
        vkWaitForFences(vk.device, 1, &frameFence, VK_TRUE, UINT64_MAX);
        vkResetFences(vk.device, 1, &frameFence);
        
        /*
        *  Acquire the "Next" Swap Chain Image
        */
        
        u32 imageIndex = UINT32_MAX;
        if (vkAcquireNextImageKHR(vk.device, vk.swapchain,
                                  UINT64_MAX, // timeout
                                  imageAvailableSemaphore,
                                  VK_NULL_HANDLE, // fence (ignored)
                                  &imageIndex) == VK_ERROR_OUT_OF_DATE_KHR)
        {
            // TODO: Handle window resize - recreate swapchain
        }
        
        assert(imageIndex != UINT32_MAX);
        
        /*
        *  Process Windows' messages
        */
        
        MSG message;
        while (PeekMessage(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        
        /*
        *  Reset and Begin Command Buffer
        */
        
        vkResetCommandBuffer(commandBuffer, 0);
        
        VkCommandBufferBeginInfo beginInfo =
        {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            NULL,
            0,
            NULL // pInheritanceInfo
        };
        
        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        
        /*
        *  Begin Render Pass
        */
        
        VkOffset2D renderAreaOffset = { 0, 0 };
        VkRect2D renderArea =
        {
            renderAreaOffset,
            vk.swapchainExtents
        };
        
        VkClearColorValue clearColor = {1, 1, 0, 1}; // yellow
        
        VkClearValue clearValue = { clearColor };
        
        VkClearValue clearValues[] = { clearValue };
        
        VkRenderPassBeginInfo renderPassBeginInfo =
        {
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            NULL,
            renderPass,
            swapchainFramebuffers[imageIndex], // framebuffer
            renderArea,
            array_count(clearValues),
            clearValues
        };
        
        vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo,
                             VK_SUBPASS_CONTENTS_INLINE);
        
        /*
        *  Finish the Command Buffer
        */
        
        // Bind the pipeline
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          graphicsPipeline);
        
        // Draw 3 vertices (triangle)
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        
        // End the render pass
        vkCmdEndRenderPass(commandBuffer);
        
        // End the command buffer
        vkEndCommandBuffer(commandBuffer);
        
        /*
        *  Submit Command Buffer
        */
        
        VkCommandBuffer commandBuffers[] = { commandBuffer };
        
        VkSemaphore imageAvailableSemaphores[] = { imageAvailableSemaphore };
        
        VkPipelineStageFlags waitStages[] =
        {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        };
        
        VkSemaphore renderFinishedSemaphores[] = { renderFinishedSemaphore };
        
        VkSubmitInfo submitInfo =
        {
            VK_STRUCTURE_TYPE_SUBMIT_INFO,
            NULL,
            array_count(imageAvailableSemaphores),
            imageAvailableSemaphores,
            waitStages,
            array_count(commandBuffers),
            commandBuffers,
            array_count(renderFinishedSemaphores),
            renderFinishedSemaphores
        };
        
        if (vkQueueSubmit(vk.graphicsAndPresentQueue, 1, &submitInfo,
                          frameFence) != VK_SUCCESS)
        {
            assert(!"failed to submit draw command buffer!");
        }
        
        /*
        *  Present the image
        */
        
        VkSwapchainKHR swapchains[] = { vk.swapchain };
        u32 imageIndices[] = { imageIndex };
        
        VkPresentInfoKHR presentInfo =
        {
            VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            NULL,
            array_count(renderFinishedSemaphores), // waitSemaphoreCount
            renderFinishedSemaphores, // pWaitSemaphores
            array_count(swapchains),
            swapchains,
            imageIndices,
            NULL, // pResults
        };
        
        if (vkQueuePresentKHR(vk.graphicsAndPresentQueue, &presentInfo) ==
            VK_ERROR_OUT_OF_DATE_KHR)
        {
            // TODO: Handle window resize - recreate swapchain
        }
    }
    
    return 0;
}