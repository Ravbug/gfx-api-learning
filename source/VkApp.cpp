#include "App.hpp"
#include <vector>
#include <set>
#if VK_AVAILABLE

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <cassert>
#include <stdexcept>
#include <format>
#include <iostream>
#include <optional>
#include <limits> 
#include <algorithm> 

#undef min
#undef max

#define VK_CHECK(a) {auto VK_CHECK_RESULT = a; assert(VK_CHECK_RESULT == VK_SUCCESS);}
#define VK_CHECK_OPT(a) {auto VK_CHECK_RESULT = a; if(VK_CHECK_RESULT != VK_SUCCESS){std::cout << std::format("VK_CHECK_OPT {}:{} failed",__FILE__,__LINE__) << std::endl;}}

#define ARRAYSIZE(a) (sizeof(a)/sizeof(a[0]))

using namespace std;

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

    VkApp* app = static_cast<VkApp*>(pUserData); // now do whatever with this

    if (messageType >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
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

void VkApp::inithook() {
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

    // message callback
    if constexpr (enableValidationLayers){
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{
           .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
           .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
           .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
           .pfnUserCallback = debugCallback,
           .pUserData = this   // optional
        };
        VK_CHECK(CreateDebugUtilsMessengerEXT(instance, &debugCreateInfo, nullptr, &debugMessenger));
    }

    // window surface
    VK_CHECK(glfwCreateWindowSurface(instance, window, nullptr, &surface));
   
    // now select and configure a device
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("No GPUs with Vulkan support");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    // find a queue of the right family
    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;
        bool isComplete() {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };
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

    constexpr static const char* const deviceExtensions[] = {
           VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    constexpr auto checkDeviceExtensionSupport = [](const VkPhysicalDevice device) -> bool{
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

    // figure out what swapchains are suported
    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
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

    constexpr auto isDeviceSuitable = [](const VkPhysicalDevice device) -> bool{
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
        std::cout << std::format("GPU: {}\nDriver {}",props.deviceName, props.driverVersion) << std::endl;
    }
   
    // next create the logical device and the queue
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    float queuePriority = 1.0f;     // required even if we only have one queue. Used to cooperatively schedule multiple queues

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value() };

    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = indices.graphicsFamily.value(),
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
    vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);

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
    auto chooseSwapExtent = [this](const VkSurfaceCapabilitiesKHR& capabilities) ->VkExtent2D {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }
        else {
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);

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

void VkApp::tickhook() {

}

void VkApp::cleanuphook() {
    if constexpr (enableValidationLayers) {
        DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
    }
    for (auto imageView : swapChainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    vkDestroySwapchainKHR(device, swapChain, nullptr);
    vkDestroyDevice(device,nullptr);
    vkDestroySurfaceKHR(instance,surface,nullptr);
    vkDestroyInstance(instance, nullptr);
}
#endif