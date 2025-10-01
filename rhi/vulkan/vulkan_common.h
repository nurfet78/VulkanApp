// engine/rhi/vulkan/vulkan_common.h
#pragma once

#include "pch.h"

#define VK_CHECK(x) do { \
    VkResult vk_result = x; \
    if (vk_result != VK_SUCCESS) { \
        throw std::runtime_error(std::string("Vulkan error: ") + #x); \
    } \
} while(0)

namespace RHI::Vulkan {

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> compute;
    std::optional<uint32_t> transfer;
    std::optional<uint32_t> present;
    
    bool IsComplete() const {
        return graphics.has_value() && present.has_value();
    }
};

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

} // namespace RHI::Vulkan