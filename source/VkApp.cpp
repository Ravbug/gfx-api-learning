#include "App.hpp"

#if VK_AVAILABLE

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cassert>
#include <stdexcept>
#include <format>
#include <iostream>
#include <optional>
#include <limits> 
#include <algorithm> 
#include <fstream>
#include <vector>
#include <set>
#include <filesystem>
#include <array>

#include <glm/glm.hpp>

// grrr...
#undef min
#undef max

#define VK_CHECK(a) {auto VK_CHECK_RESULT = a; assert(VK_CHECK_RESULT == VK_SUCCESS);}
#define VK_CHECK_OPT(a) {auto VK_CHECK_RESULT = a; if(VK_CHECK_RESULT != VK_SUCCESS){std::cout << std::format("VK_CHECK_OPT {}:{} failed",__FILE__,__LINE__) << std::endl;}}
#define VK_VALID(a) {assert(a != VK_NULL_HANDLE);}

using namespace std;

struct Vertex {
    glm::vec2 pos;
    glm::vec3 color;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{
            .binding = 0,           
            .stride = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX    // how to traverse the data. for vertex inputs, use Vertex. for Instance buffers, use Instance
        };

        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions() {
        // look at the shader to see where these come from. they match the layout Inputs
        std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        return attributeDescriptions;
    }

};

static constexpr Vertex vertices[] = {
    {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
};

// ideally these would go in some kind of ADT
static VkInstance instance;
static VkDebugUtilsMessengerEXT debugMessenger;
static VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
static VkDevice device;
static VkQueue graphicsQueue;
static VkSurfaceKHR surface;
static VkQueue presentQueue;
static VkSwapchainKHR swapChain;
static VkFormat swapChainImageFormat;
static VkExtent2D swapChainExtent;
static std::vector<VkImage> swapChainImages;
static std::vector<VkImageView> swapChainImageViews;
static std::vector<VkFramebuffer> swapChainFramebuffers;

static VkShaderModule vertShaderModule;
static VkShaderModule fragShaderModule;

static VkPipeline graphicsPipeline;
static VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
static VkRenderPass renderPass = VK_NULL_HANDLE;

static VkCommandPool commandPool;
static VkCommandBuffer commandBuffer;

// meshdata
static VkBuffer vertexBuffer;
static VkDeviceMemory vertexBufferMemory;

//synchronization primitves
static VkSemaphore imageAvailableSemaphore;
static VkSemaphore renderFinishedSemaphore;
static VkFence inFlightFence;

// layers we want
static const char* const validationLayers[] = {
    "VK_LAYER_KHRONOS_validation"
};

constexpr bool enableValidationLayers =
#ifdef NDEBUG
    false;
#else
    true;
#endif

// vulkan calls this on debug message
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        std::cout << std::format("validation layer: {}", pCallbackData->pMessage) << std::endl;
    }

    return VK_FALSE;
}

// helper to create the debug messenger
// by looking up the memory address of the extension function
VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

// unload the debug messenger created above
void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

// helper to read a binary file
static std::vector<char> readFile(const std::filesystem::path& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error(std::format("failed to open {}",filename.string()));
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

// figure out what swapchains are suported
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

void createInstance() {
    // init the global information
    VkApplicationInfo appInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Hello Triangle",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0
    };
    VkInstanceCreateInfo instanceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = 0,
        .enabledExtensionCount = 0
    };

    // validation layers
    std::vector<VkLayerProperties> availableLayers;
    if constexpr (enableValidationLayers) {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        availableLayers.resize(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for (const char* layerName : validationLayers) {

            if (std::find_if(availableLayers.begin(), availableLayers.end(), [layerName](auto& layerProperties) {
                return strcmp(layerName, layerProperties.layerName) == 0;
                }
            ) == availableLayers.end()) {
                throw std::runtime_error(std::format("required validation layer {} not found", layerName));
            }
        }
        instanceCreateInfo.enabledLayerCount = ARRAYSIZE(validationLayers);
        instanceCreateInfo.ppEnabledLayerNames = validationLayers;
    }

    // load GLFW's specific extensions for Vulkan
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&instanceCreateInfo.enabledExtensionCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + instanceCreateInfo.enabledExtensionCount);
    if constexpr (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME); // debug callback
    }
    instanceCreateInfo.ppEnabledExtensionNames = extensions.data();
    instanceCreateInfo.enabledExtensionCount = extensions.size();
    // when doing own implementation, use vkEnumerateInstanceExtensionProperties
    // https://vulkan-tutorial.com/en/Drawing_a_triangle/Setup/Instance


    // init vulkan
    VK_CHECK(vkCreateInstance(&instanceCreateInfo, nullptr, &instance));
}

void setupDebugMessenger() {
    // message callback
    if constexpr (enableValidationLayers) {
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{
           .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
           .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
           .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
           .pfnUserCallback = debugCallback,
           .pUserData = nullptr   // optional
        };
        VK_CHECK(CreateDebugUtilsMessengerEXT(instance, &debugCreateInfo, nullptr, &debugMessenger));
    }
}

void createSurface(VkApp* app) {
    // window surface
    VK_CHECK(glfwCreateWindowSurface(instance, app->window, nullptr, &surface));
}

constexpr static const char* const deviceExtensions[] = {
       VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

constexpr auto checkDeviceExtensionSupport = [](const VkPhysicalDevice device) -> bool {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(std::begin(deviceExtensions), std::end(deviceExtensions));

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
};


constexpr auto querySwapChainSupport = [](const VkPhysicalDevice device) {
    // inquire surface capabilities
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    // get the formats that are supported
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    // get the present modes
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
};

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

QueueFamilyIndices selectPhysicalAndLogicalDevice() {
    // now select and configure a device
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("No GPUs with Vulkan support");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    // find a queue of the right family
    
    constexpr auto findQueueFamilies = [](VkPhysicalDevice device) -> QueueFamilyIndices {
        QueueFamilyIndices indices;
        // Logic to find queue family indices to populate struct with

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        int i = 0;
        for (const auto& queueFamily : queueFamilies) {
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphicsFamily = i;
            }
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
            if (presentSupport) {
                indices.presentFamily = i;
            }
            i++;
        }
        return indices;
    };


    constexpr auto isDeviceSuitable = [](const VkPhysicalDevice device) -> bool {
        // look for all the features we want
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);

        VkPhysicalDeviceFeatures deviceFeatures;
        vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

        auto queueFamilyData = findQueueFamilies(device);

        auto extensionsSupported = checkDeviceExtensionSupport(device);

        bool swapChainAdequate = false;
        if (extensionsSupported) {
            SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
            swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty() && swapChainAdequate;
        }

        // right now we don't care so pick any gpu
        // in the future implement a scoring system to pick the best device
        return deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && queueFamilyData.isComplete() && checkDeviceExtensionSupport(device);
    };
    for (const auto& device : devices) {
        if (isDeviceSuitable(device)) {
            physicalDevice = device;
            break;
        }
    }
    if (physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
    {
        // print what gpu we are using
        VkPhysicalDeviceProperties props;;
        vkGetPhysicalDeviceProperties(physicalDevice, &props);
        std::cout << std::format("GPU: {}\nDriver {}", props.deviceName, props.driverVersion) << std::endl;
    }

    // next create the logical device and the queue
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    float queuePriority = 1.0f;     // required even if we only have one queue. Used to cooperatively schedule multiple queues

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value() };

    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queueFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
        };
        queueCreateInfos.push_back(queueCreateInfo);
    }
    VkPhysicalDeviceFeatures deviceFeatures{};      // we don't yet need anything
    VkDeviceCreateInfo deviceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = static_cast<decltype(VkDeviceCreateInfo::queueCreateInfoCount)>(queueCreateInfos.size()),
        .pQueueCreateInfos = queueCreateInfos.data(),      // could pass an array here if we were making more than one queue
        .enabledExtensionCount = ARRAYSIZE(deviceExtensions),             // device-specific extensions are ignored on later vulkan versions but we set it anyways
        .ppEnabledExtensionNames = deviceExtensions,
        .pEnabledFeatures = &deviceFeatures,
    };
    if constexpr (enableValidationLayers) {
        deviceCreateInfo.enabledLayerCount = ARRAYSIZE(validationLayers);
        deviceCreateInfo.ppEnabledLayerNames = validationLayers;
    }
    VK_CHECK(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device));
    vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);    // 0 because we only have 1 queue
    VK_VALID(graphicsQueue);
    vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
    VK_VALID(presentQueue);

    return indices;
}

void setupSwapChain(VkApp* app, const QueueFamilyIndices& indices) {
    // setup surface
    constexpr auto chooseSwapSurfaceFormat = [](const std::vector<VkSurfaceFormatKHR>& availableFormats) -> VkSurfaceFormatKHR {
        // we want BGRA8 SRGB in nonlinear space
        for (const auto& availableFormat : availableFormats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return availableFormat;
            }
        }

        //otherwise hope the first one is good enough
        return availableFormats[0];
    };
    constexpr auto chooseSwapPresentMode = [](const std::vector<VkPresentModeKHR>& availablePresentModes) -> VkPresentModeKHR {
        for (const auto& availablePresentMode : availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return availablePresentMode;    // use Mailbox on high-perf devices
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;        // otherwise use FIFO when on low-power devices, like a mobile phone
    };
    auto chooseSwapExtent = [app](const VkSurfaceCapabilitiesKHR& capabilities) ->VkExtent2D {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }
        else {
            int width, height;
            glfwGetFramebufferSize(app->window, &width, &height);

            VkExtent2D actualExtent = {
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)
            };

            actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

            return actualExtent;
        }
    };

    // configure the swap chain stuff
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;  // we want one extra image than necessary to reduce latency (no waiting for the driver)
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }
    VkSwapchainCreateInfoKHR swapchainCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,      // always 1 unless we are doing stereoscopic 3D
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,   // use VK_IMAGE_USAGE_TRANSFER_DST_BIT for offscreen rendering
        .preTransform = swapChainSupport.capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,     // we don't care about pixels that are obscured
        .oldSwapchain = VK_NULL_HANDLE  // future issue
    };
    uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };
    if (indices.graphicsFamily != indices.presentFamily) {
        swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchainCreateInfo.queueFamilyIndexCount = 2;
        swapchainCreateInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else {
        swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchainCreateInfo.queueFamilyIndexCount = 0; // Optional
        swapchainCreateInfo.pQueueFamilyIndices = nullptr; // Optional
    }

    VK_CHECK(vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapChain));
    // remember these values
    swapChainImageFormat = surfaceFormat.format;
    swapChainExtent = extent;

    // get the swap chain images
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());
}

void createSwapChainImageViews()
{
    // create image views from images
    swapChainImageViews.resize(swapChainImages.size());
    for (size_t i = 0; i < swapChainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapChainImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapChainImageFormat,
            .components{
                .r = VK_COMPONENT_SWIZZLE_IDENTITY, // we don't want any swizzling
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY
        },
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,    // mipmap and layer info (we don't want any here)
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
        }
        };
        VK_CHECK(vkCreateImageView(device, &createInfo, nullptr, &swapChainImageViews[i]));
    }
}

void cleanupSwapChain() {
    for (size_t i = 0; i < swapChainFramebuffers.size(); i++) {
        vkDestroyFramebuffer(device, swapChainFramebuffers[i], nullptr);
    }

    for (size_t i = 0; i < swapChainImageViews.size(); i++) {
        vkDestroyImageView(device, swapChainImageViews[i], nullptr);
    }

    vkDestroySwapchainKHR(device, swapChain, nullptr);
}


void createRenderPass() {
    VkAttachmentDescription colorAttachment{
        .format = swapChainImageFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,  // what to do before and after
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR  // see https://vulkan-tutorial.com/en/Drawing_a_triangle/Graphics_pipeline_basics/Render_passes (VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) for more info
    };

    //subpass
    VkAttachmentReference colorAttachmentRef{
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };
    VkSubpassDescription subpass{
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,                  // for MRT bind the textures here
        .pColorAttachments = &colorAttachmentRef
    };

    // dependencies allow ensuring that passes execute at the right time
    VkSubpassDependency dependency{
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    // full pass
    VkRenderPassCreateInfo renderPassInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorAttachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency
    };
    VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
}

void createGraphicsPipeline() {
    // create the pipelines
    auto vertShaderCode = readFile("vk.vert.spv");
    auto fragShaderCode = readFile("vk.frag.spv");

    constexpr auto createShaderModule = [](const std::vector<char>& code) -> VkShaderModule {
        VkShaderModuleCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = code.size(),    // in bytes, not multiples of uint32
            .pCode = reinterpret_cast<const uint32_t*>(code.data())
        };
        VkShaderModule shaderModule;
        VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule));
        return shaderModule;
    };

    vertShaderModule = createShaderModule(vertShaderCode);
    fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vertShaderModule,
        .pName = "main"     // for if different entrypoints are desired
        // (not listed) pSpecializationInfo allows setting constants / defines when compiling the shader, to make generating variants easier
    };
    VkPipelineShaderStageCreateInfo fragShaderStageInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = fragShaderModule,
        .pName = "main"
    };
    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    // this allows for some minor tweaks to the pipeline object after it's created
    // at draw time, the values must be specified (required)
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };

    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    // vertex format
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDescription,      // optional
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
        .pVertexAttributeDescriptions = attributeDescriptions.data(),    // optional
    };

    // trilist, tristrip, etc
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE      // for STRIP topology
    };

    // the viewport
    VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)swapChainExtent.width,
        .height = (float)swapChainExtent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f        // depth values must be within [0,1] but minDepth does not need to be lower than maxDepth
    };

    // the scissor
    VkRect2D scissor{
        scissor.offset = {0, 0},
        scissor.extent = swapChainExtent
    };
    // here's where we set the dynamic pipeline states
    VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor       // arrays go here, but using multiple requires enabling a GPU feature
    };

    // fragment stage config
    VkPipelineRasterizationStateCreateInfo rasterizer{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,    // if set to true, fragments out of range will be clamped instead of clipped, which we rarely want (example: shadow volumes, no need for end caps)
        .rasterizerDiscardEnable = VK_FALSE, // if true, output to the framebuffer is disabled
        .polygonMode = VK_POLYGON_MODE_FILL,        // lines, points, fill (anything other than fill requires a GPU feature)
        .cullMode = VK_CULL_MODE_BACK_BIT,      // front vs backface culling
        .frontFace = VK_FRONT_FACE_CLOCKWISE,   // CW vs CCW 
        .depthBiasEnable = VK_FALSE,            // depth bias is useful for shadow maps
        .depthBiasConstantFactor = 0.0f,    // the next 3 are optional
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f,                       // thickness > 1 requires the wideLines GPU feature
    };

    // a way to configure hardware anti aliasing
    // this only occurs along geometry edges
    VkPipelineMultisampleStateCreateInfo multisampling{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,   // the rest are optional
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE
    };

    // blending modes for render targets
    VkPipelineColorBlendAttachmentState colorBlendAttachment{   // make one of these for each target texture
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE, //the next 6 are optional
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO, //optional
        .colorBlendOp = VK_BLEND_OP_ADD, // Optional
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo colorBlending{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,    //optional
        .attachmentCount = 1,           // for MRT
        .pAttachments = &colorBlendAttachment,  // specify all the attachments here
        .blendConstants = {0,0,0,0},        // optional
    };

    // piepline layout
    // here is were you declare uniforms
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 0,    // the rest are optional
        .pSetLayouts = nullptr,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr
    };
    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

    // create the pipeline object
    VkGraphicsPipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = shaderStages,
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = nullptr,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = pipelineLayout,
        .renderPass = renderPass,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE, // optional
        .basePipelineIndex = -1 // optional
    };
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline));
}

void createFramebuffers(){
    swapChainFramebuffers.resize(swapChainImageViews.size());
    for (size_t i = 0; i < swapChainImageViews.size(); i++) {
        VkImageView attachments[] = {
            swapChainImageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo{
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = renderPass,
            .attachmentCount = 1,
            .pAttachments = attachments,
            .width = swapChainExtent.width,
            .height = swapChainExtent.height,
            .layers = 1
        };
        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}

void createCommandPool(const QueueFamilyIndices& queueFamilyIndices) {
    VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,       // use this value if we want to write over the command buffer (ie for generating it every frame)
        .queueFamilyIndex = queueFamilyIndices.graphicsFamily.value()
    };
    VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool));
};

uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    // find a memory type suitable for the buffer
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        // needs to have the right support
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

void createVertexBuffer() {
    VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(vertices),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,  // could have multiple here if the buffer was used in multiple different stages
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,    // only the Graphics queue is using this
    };
    VK_CHECK(vkCreateBuffer(device, &bufferInfo, nullptr, &vertexBuffer));

    // the buffer doesn't have any memory assigned yet, so now we assign some
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, vertexBuffer, &memRequirements);

    // prepare to allocate the memory
    VkMemoryAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        // note that the data might not be on the GPU if we don't use VK_MEMORY_PROPERTY_HOST_COHERENT_BIT. This bit may decrease performance but not in a meaningful way.
        .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };
    // allocate it
    VK_CHECK(vkAllocateMemory(device, &allocInfo, nullptr, &vertexBufferMemory));

    // associate it with the buffer
    vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0);

    // fill the buffer with data
    void* data;
    vkMapMemory(device, vertexBufferMemory, 0, bufferInfo.size, 0, &data);
    memcpy(data, vertices, (size_t)bufferInfo.size);
    vkUnmapMemory(device, vertexBufferMemory);

}

void createCommandBuffer() {
    // setup creating the command buffer
    VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));
}

void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    // start recording commands
    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = 0,
        .pInheritanceInfo = nullptr,
    };
    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    // setup the pass
    VkClearValue clearColor = { {{0.0f, 0.0f, 0.0f, 1.0f}} };

    VkRenderPassBeginInfo renderPassInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = renderPass,
        .framebuffer = swapChainFramebuffers[imageIndex],
        .renderArea = {
            .offset = {0, 0},
            .extent = swapChainExtent
        },
        .clearValueCount = 1,
        .pClearValues = &clearColor,
    };

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // drawing commands
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    // associate vertex data
    VkBuffer vertexBuffers[] = { vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    // the dynamic properties we have to set (that we declared earlier as dynamic)
    VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(swapChainExtent.width),
        .height = static_cast<float>(swapChainExtent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{
        .offset = {0, 0},
        .extent = swapChainExtent
    };
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdDraw(commandBuffer, ARRAYSIZE(vertices), 1, 0, 0);

    vkCmdEndRenderPass(commandBuffer);

    VK_CHECK(vkEndCommandBuffer(commandBuffer));
}

void createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };
    VkFenceCreateInfo fenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT       // create it already signaled, so that we won't block forever waiting for a render that won't happen on the first call to drawFrame
    };
    VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore));
    VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore));
    VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &inFlightFence));
}

void recreateSwapChain(VkApp* app, const QueueFamilyIndices& indices) {
    // resize protection - spin until a nonzero size is provided
    int width = 0, height = 0;
    glfwGetFramebufferSize(app->window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(app->window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(device);

    cleanupSwapChain();

    setupSwapChain(app, indices);
    createSwapChainImageViews();
    createFramebuffers();
}

static QueueFamilyIndices global_indices;
static VkApp* global_app = nullptr;;

void VkApp::inithook() {
    global_app = this;
    createInstance();
    setupDebugMessenger();
    createSurface(this);
    auto indices = selectPhysicalAndLogicalDevice();
    setupSwapChain(this,indices);
    createSwapChainImageViews();
    global_indices = indices;

    // render pass
    createRenderPass();
    createGraphicsPipeline();
    createFramebuffers();

    // command buffers
    createCommandPool(indices);
    createVertexBuffer();
    createCommandBuffer();
    createSyncObjects();
}

void drawFrame() {
    // get the next image in the swap chain to use
    uint32_t imageIndex;
    auto result = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    // wait for the previous frame
    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);

    // if the image is out of date, then we recreate the chains
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain(global_app,global_indices);
        return;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw runtime_error("failed to acquire next swapchain image");
    }

    // reset the fence, we are now submitting work
    vkResetFences(device, 1, &inFlightFence); 

    // populate the command buffer
    vkResetCommandBuffer(commandBuffer, 0);
    recordCommandBuffer(commandBuffer, imageIndex);

    // prepare to submit the command buffer 
    VkSemaphore waitSemaphores[] = { imageAvailableSemaphore };
    VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSubmitInfo submitInfo{
       .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
       .waitSemaphoreCount = 1,
       .pWaitSemaphores = waitSemaphores,
       .pWaitDstStageMask = waitStages,
       .commandBufferCount = 1,
       .pCommandBuffers = &commandBuffer,
       .signalSemaphoreCount = 1,
       .pSignalSemaphores = signalSemaphores
    };

    // submit it to the queue!
    VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFence));

    // then present it
    VkSwapchainKHR swapChains[] = { swapChain };
    VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = signalSemaphores,
        .swapchainCount = 1,
        .pSwapchains = swapChains,
        .pImageIndices = &imageIndex,
        .pResults = nullptr         // optional
    };
    result = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreateSwapChain(global_app, global_indices);
    }
    else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }
}

void VkApp::tickhook() {
    drawFrame();
}

void VkApp::cleanuphook() {
    vkDeviceWaitIdle(device);

    cleanupSwapChain();

    vkDestroyBuffer(device, vertexBuffer, nullptr);
    vkFreeMemory(device, vertexBufferMemory, nullptr);

    if constexpr (enableValidationLayers) {
        DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
    }

    vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
    vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
    vkDestroyFence(device, inFlightFence, nullptr);

    for (auto imageView : swapChainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
    for (auto framebuffer : swapChainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);

    vkDestroySwapchainKHR(device, swapChain, nullptr);
    vkDestroyDevice(device,nullptr);
    vkDestroySurfaceKHR(instance,surface,nullptr);
    vkDestroyInstance(instance, nullptr);
}

#endif