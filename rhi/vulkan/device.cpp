// engine/rhi/vulkan/device.cpp
#include "device.h"
#include "core/window.h"

namespace RHI::Vulkan {
	
	
Device::Device(Core::Window* window, bool enableValidation) 
    : m_validationEnabled(enableValidation) {
    
    VK_CHECK(volkInitialize());
    
    CreateInstance(enableValidation);
    volkLoadInstance(m_instance);
    
    SetupDebugMessenger();
    CreateSurface(window);
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateAllocator();
}

Device::~Device() noexcept {

    m_descriptorAllocator.~DescriptorAllocator(); // очищает пулы

    // Critical: Correct destruction order
    if (m_allocator) {
        vmaDestroyAllocator(m_allocator);
    }
    
    if (m_device) {
        vkDestroyDevice(m_device, nullptr);
    }
    
    if (m_validationEnabled && m_debugMessenger) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            m_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func) {
            func(m_instance, m_debugMessenger, nullptr);
        }
    }
    
    if (m_surface) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }
    
    if (m_instance) {
        vkDestroyInstance(m_instance, nullptr);
    }
}

// Thread-safe queue submit methods
VkResult Device::SubmitGraphics(const VkSubmitInfo* submitInfo, VkFence fence) {
    std::lock_guard lock(m_graphicsQueueMutex);
    return vkQueueSubmit(m_graphicsQueue, 1, submitInfo, fence);
}

VkResult Device::SubmitCompute(const VkSubmitInfo* submitInfo, VkFence fence) {
    std::lock_guard lock(m_computeQueueMutex);
    return vkQueueSubmit(m_computeQueue, 1, submitInfo, fence);
}

VkResult Device::SubmitTransfer(const VkSubmitInfo* submitInfo, VkFence fence) {
    std::lock_guard lock(m_transferQueueMutex);
    return vkQueueSubmit(m_transferQueue, 1, submitInfo, fence);
}

////////////////////////////////////////////////////////////////


void Device::CreateInstance(bool enableValidation) {
    // Check validation layer support
    if (enableValidation) {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
        
        for (const char* layerName : m_validationLayers) {
            bool found = false;
            for (const auto& layerProperties : availableLayers) {
                if (strcmp(layerName, layerProperties.layerName) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                throw std::runtime_error("Validation layer not available");
            }
        }
    }
    
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Meadow World";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Custom Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;
    
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    
    auto extensions = GetRequiredExtensions(enableValidation);
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidation) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();
        
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = DebugCallback;
        
        createInfo.pNext = &debugCreateInfo;
    }
    
    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_instance));
}

void Device::SetupDebugMessenger() {
    if (!m_validationEnabled) return;
    
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugCallback;
    
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        m_instance, "vkCreateDebugUtilsMessengerEXT");
    if (func) {
        VK_CHECK(func(m_instance, &createInfo, nullptr, &m_debugMessenger));
    }
}

void Device::CreateSurface(Core::Window* window) {
    VK_CHECK(window->CreateWindowSurface(m_instance, &m_surface));
}

void Device::PickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    
    if (deviceCount == 0) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support");
    }
    
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());
    
    // Find suitable device, prefer discrete GPU
    VkPhysicalDevice discreteGPU = VK_NULL_HANDLE;
    VkPhysicalDevice integratedGPU = VK_NULL_HANDLE;
    
    for (const auto& device : devices) {
        if (IsDeviceSuitable(device)) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                discreteGPU = device;
                break;
            } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
                integratedGPU = device;
            }
        }
    }
    
    m_physicalDevice = discreteGPU ? discreteGPU : integratedGPU;
    
    if (m_physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to find suitable GPU");
    }
    
    vkGetPhysicalDeviceProperties(m_physicalDevice, &m_properties);
    m_queueFamilies = FindQueueFamilies(m_physicalDevice);
    
    std::cout << "Selected GPU: " << m_properties.deviceName << std::endl;
}

void Device::CreateLogicalDevice() {
    // Сначала убедимся, что queue families найдены
    if (!m_queueFamilies.graphics.has_value() || !m_queueFamilies.present.has_value()) {
        throw std::runtime_error("Required queue families not found");
    }

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        m_queueFamilies.graphics.value(),
        m_queueFamilies.present.value()
    };

    // Добавляем compute и transfer если они есть и отличаются
    if (m_queueFamilies.compute.has_value()) {
        uniqueQueueFamilies.insert(m_queueFamilies.compute.value());
    }
    if (m_queueFamilies.transfer.has_value()) {
        uniqueQueueFamilies.insert(m_queueFamilies.transfer.value());
    }

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // Vulkan 1.3 features
    VkPhysicalDeviceVulkan13Features vulkan13Features{};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13Features.dynamicRendering = VK_TRUE;
    vulkan13Features.synchronization2 = VK_TRUE;
    vulkan13Features.maintenance4 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features vulkan12Features{};
    vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12Features.pNext = &vulkan13Features;
    vulkan12Features.timelineSemaphore = VK_TRUE;
    vulkan12Features.bufferDeviceAddress = VK_TRUE;
    vulkan12Features.descriptorIndexing = VK_TRUE;
    vulkan12Features.descriptorBindingPartiallyBound = VK_TRUE;
    vulkan12Features.runtimeDescriptorArray = VK_TRUE;

    VkPhysicalDeviceFeatures2 deviceFeatures{};
    deviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures.pNext = &vulkan12Features;
    deviceFeatures.features.samplerAnisotropy = VK_TRUE;
    deviceFeatures.features.fillModeNonSolid = VK_TRUE;
    deviceFeatures.features.wideLines = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &deviceFeatures;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(m_deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();

    if (m_validationEnabled) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();
    }

    // Создаем устройство
    VK_CHECK(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device));

    // ВАЖНО: Загружаем функции устройства
    volkLoadDevice(m_device);

    // Получаем очереди - с проверками!
    vkGetDeviceQueue(m_device, m_queueFamilies.graphics.value(), 0, &m_graphicsQueue);
    if (!m_graphicsQueue) {
        throw std::runtime_error("Failed to get graphics queue");
    }

    vkGetDeviceQueue(m_device, m_queueFamilies.present.value(), 0, &m_presentQueue);
    if (!m_presentQueue) {
        throw std::runtime_error("Failed to get present queue");
    }

    // Compute queue
    if (m_queueFamilies.compute.has_value() &&
        m_queueFamilies.compute.value() != m_queueFamilies.graphics.value()) {
        vkGetDeviceQueue(m_device, m_queueFamilies.compute.value(), 0, &m_computeQueue);
    }
    else {
        m_computeQueue = m_graphicsQueue; // Используем graphics как fallback
    }

    // Transfer queue  
    if (m_queueFamilies.transfer.has_value() &&
        m_queueFamilies.transfer.value() != m_queueFamilies.graphics.value()) {
        vkGetDeviceQueue(m_device, m_queueFamilies.transfer.value(), 0, &m_transferQueue);
    }
    else {
        m_transferQueue = m_graphicsQueue; // Используем graphics как fallback
    }

    // Финальная проверка
    std::cout << "Device queues obtained:" << std::endl;
    std::cout << "  Graphics Queue: " << (m_graphicsQueue ? "OK" : "FAILED") << std::endl;
    std::cout << "  Present Queue: " << (m_presentQueue ? "OK" : "FAILED") << std::endl;
    std::cout << "  Compute Queue: " << (m_computeQueue ? "OK" : "FAILED") << std::endl;
    std::cout << "  Transfer Queue: " << (m_transferQueue ? "OK" : "FAILED") << std::endl;
}

void Device::CreateAllocator() {
    VmaVulkanFunctions vulkanFunctions = {};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    vulkanFunctions.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
    vulkanFunctions.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
    vulkanFunctions.vkAllocateMemory = vkAllocateMemory;
    vulkanFunctions.vkFreeMemory = vkFreeMemory;
    vulkanFunctions.vkMapMemory = vkMapMemory;
    vulkanFunctions.vkUnmapMemory = vkUnmapMemory;
    vulkanFunctions.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
    vulkanFunctions.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
    vulkanFunctions.vkBindBufferMemory = vkBindBufferMemory;
    vulkanFunctions.vkBindImageMemory = vkBindImageMemory;
    vulkanFunctions.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
    vulkanFunctions.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
    vulkanFunctions.vkCreateBuffer = vkCreateBuffer;
    vulkanFunctions.vkDestroyBuffer = vkDestroyBuffer;
    vulkanFunctions.vkCreateImage = vkCreateImage;
    vulkanFunctions.vkDestroyImage = vkDestroyImage;
    vulkanFunctions.vkCmdCopyBuffer = vkCmdCopyBuffer;

    // Для buffer device address (Vulkan 1.2+)
    vulkanFunctions.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2;
    vulkanFunctions.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2;
    vulkanFunctions.vkBindBufferMemory2KHR = vkBindBufferMemory2;
    vulkanFunctions.vkBindImageMemory2KHR = vkBindImageMemory2;
    vulkanFunctions.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_device;
    allocatorInfo.instance = m_instance;
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &m_allocator));
}

bool Device::IsDeviceSuitable(VkPhysicalDevice device) {
    QueueFamilyIndices indices = FindQueueFamilies(device);
    bool extensionsSupported = CheckDeviceExtensionSupport(device);
    
    bool swapchainAdequate = false;
    if (extensionsSupported) {
        SwapchainSupportDetails swapchainSupport = QuerySwapchainSupport(device);
        swapchainAdequate = !swapchainSupport.formats.empty() && 
                           !swapchainSupport.presentModes.empty();
    }
    
    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);
    
    return indices.IsComplete() && extensionsSupported && swapchainAdequate &&
           supportedFeatures.samplerAnisotropy;
}

QueueFamilyIndices Device::FindQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        const auto& queueFamily = queueFamilies[i];

        // Graphics queue
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics = i;
        }

        // Dedicated compute queue (без graphics)
        if ((queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            indices.compute = i;
        }

        // Dedicated transfer queue (без graphics и compute)
        if ((queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            !(queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)) {
            indices.transfer = i;
        }

        // Present support
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport) {
            indices.present = i;
        }
    }

    // ВАЖНО: Если нет отдельных очередей, используем graphics queue
    if (!indices.compute.has_value()) {
        indices.compute = indices.graphics;
    }
    if (!indices.transfer.has_value()) {
        indices.transfer = indices.graphics;
    }

    return indices;
}

bool Device::CheckDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());
    
    std::set<std::string> requiredExtensions(m_deviceExtensions.begin(), m_deviceExtensions.end());
    
    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }
    
    return requiredExtensions.empty();
}

std::vector<const char*> Device::GetRequiredExtensions(bool enableValidation) {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    
    if (enableValidation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    
    return extensions;
}

SwapchainSupportDetails Device::QuerySwapchainSupport(VkPhysicalDevice device) const {
    SwapchainSupportDetails details;
    
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);
    
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);
    
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, details.formats.data());
    }
    
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);
    
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, details.presentModes.data());
    }
    
    return details;
}

SwapchainSupportDetails Device::QuerySwapchainSupport() const {
    return QuerySwapchainSupport(m_physicalDevice);
}

void Device::SetObjectName(uint64_t object, VkObjectType type, const char* name) const {
    if (!m_validationEnabled) return;
    
    VkDebugUtilsObjectNameInfoEXT nameInfo{};
    nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.objectType = type;
    nameInfo.objectHandle = object;
    nameInfo.pObjectName = name;
    
    vkSetDebugUtilsObjectNameEXT(m_device, &nameInfo);
}

void Device::BeginDebugLabel(VkCommandBuffer cmd, const char* label, const std::array<float, 4>& color) const {
    if (!m_validationEnabled) return;
    
    VkDebugUtilsLabelEXT labelInfo{};
    labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    labelInfo.pLabelName = label;
    memcpy(labelInfo.color, color.data(), sizeof(labelInfo.color));
    
    vkCmdBeginDebugUtilsLabelEXT(cmd, &labelInfo);
}

VkFormat Device::FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        }
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }
    throw std::runtime_error("Failed to find supported format!");
}

void Device::EndDebugLabel(VkCommandBuffer cmd) const {
    if (!m_validationEnabled) return;
    vkCmdEndDebugUtilsLabelEXT(cmd);
}

VKAPI_ATTR VkBool32 VKAPI_CALL Device::DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "Validation layer: " << pCallbackData->pMessage << std::endl;
    }
    
    return VK_FALSE;
}

} // namespace RHI::Vulkan