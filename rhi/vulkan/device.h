// engine/rhi/vulkan/device.h
#pragma once

#include "vulkan_common.h"
#include "rhi/vulkan/descriptor_allocator.h"

namespace Core { class Window; }

namespace RHI::Vulkan {

struct DeviceFeatures {
    bool dynamicRendering = true;
    bool synchronization2 = true;
    bool timelineSemaphore = true;
    bool maintenance4 = true;
    bool bufferDeviceAddress = true;
    bool descriptorIndexing = false;
    bool meshShader = false;
    bool rayTracing = false;
    bool variableRateShading = false;
};


class Device {
public:
    explicit Device(Core::Window* window, bool enableValidation = true);
    ~Device() noexcept;
    
    // Delete copy/move
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;
    
    // Getters
    VkInstance GetInstance() const { return m_instance; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }
    VkDevice GetDevice() const { return m_device; }
    VkSurfaceKHR GetSurface() const { return m_surface; }
    VmaAllocator GetAllocator() const { return m_allocator; }

    RHI::Vulkan::DescriptorAllocator* GetDescriptorAllocator() { return &m_descriptorAllocator; }
    RHI::Vulkan::DescriptorLayoutCache* GetDescriptorLayoutCache() { return &m_descriptorLayoutCache; }
    
    // Thread-safe queue access with submit mutex
    VkQueue GetGraphicsQueue() const { return m_graphicsQueue; }
    VkQueue GetComputeQueue() const { return m_computeQueue; }
    VkQueue GetTransferQueue() const { return m_transferQueue; }
    VkQueue GetPresentQueue() const { return m_presentQueue; }
    
    // Thread-safe submit
    VkResult SubmitGraphics(const VkSubmitInfo* submitInfo, VkFence fence);
    VkResult SubmitCompute(const VkSubmitInfo* submitInfo, VkFence fence);
    VkResult SubmitTransfer(const VkSubmitInfo* submitInfo, VkFence fence);
    
    uint32_t GetGraphicsQueueFamily() const {
        return m_queueFamilies.graphics.value_or(0);
    }
    uint32_t GetComputeQueueFamily() const {
        return m_queueFamilies.compute.value_or(GetGraphicsQueueFamily());
    }
    uint32_t GetTransferQueueFamily() const {
        return m_queueFamilies.transfer.value_or(GetGraphicsQueueFamily());
    }
    uint32_t GetPresentQueueFamily() const {
        return m_queueFamilies.present.value_or(GetGraphicsQueueFamily());
    }

    // Добавить методы для проверки наличия отдельных очередей:
    bool HasDedicatedTransferQueue() const {
        return m_queueFamilies.transfer.has_value() &&
            m_queueFamilies.transfer != m_queueFamilies.graphics;
    }
    bool HasDedicatedComputeQueue() const {
        return m_queueFamilies.compute.has_value() &&
            m_queueFamilies.compute != m_queueFamilies.graphics;
    }
    
    const VkPhysicalDeviceProperties& GetProperties() const { return m_properties; }
    VkPhysicalDeviceLimits GetLimits() const { 
        std::lock_guard lock(m_propertiesMutex);
        return m_properties.limits; 
    }
    const DeviceFeatures& GetEnabledFeatures() const { return m_enabledFeatures; }
    
    SwapchainSupportDetails QuerySwapchainSupport() const;
    SwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice device) const;
    
    // Debug markers
    void SetObjectName(uint64_t object, VkObjectType type, const char* name) const;
    void BeginDebugLabel(VkCommandBuffer cmd, const char* label, const std::array<float, 4>& color = {1, 1, 1, 1}) const;
    void EndDebugLabel(VkCommandBuffer cmd) const;

    VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const;
    
    void WaitIdle() const { vkDeviceWaitIdle(m_device); }

private:
    void CreateInstance(bool enableValidation);
    void SetupDebugMessenger();
    void CreateSurface(Core::Window* window);
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateAllocator();
    
    bool IsDeviceSuitable(VkPhysicalDevice device);
    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
    bool CheckDeviceExtensionSupport(VkPhysicalDevice device);
    std::vector<const char*> GetRequiredExtensions(bool enableValidation);
    
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);
    
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_computeQueue = VK_NULL_HANDLE;
    VkQueue m_transferQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    
    // Queue submit mutexes for thread safety
    mutable std::mutex m_graphicsQueueMutex;
    mutable std::mutex m_computeQueueMutex;
    mutable std::mutex m_transferQueueMutex;
    mutable std::mutex m_propertiesMutex;
    
    QueueFamilyIndices m_queueFamilies;
    VkPhysicalDeviceProperties m_properties{};
    DeviceFeatures m_enabledFeatures{};
    
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    
    DescriptorAllocator m_descriptorAllocator{ this };
    DescriptorLayoutCache m_descriptorLayoutCache{ this };

    bool m_validationEnabled = false;
    
    const std::vector<const char*> m_deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
        VK_KHR_MAINTENANCE_4_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
        VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME
    };
    
    const std::vector<const char*> m_validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };
};

} // namespace RHI::Vulkan