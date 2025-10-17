#include "core_context.h"
#include "rhi/vulkan/device.h"

namespace Core {

    CoreContext::CoreContext(RHI::Vulkan::Device* device) : m_device(device) {
        if (!device) {
            throw std::invalid_argument("CoreContext: device cannot be null");
        }
    }

    CoreContext::~CoreContext() {
        if (m_device && m_commandPoolManager) {
            vkDeviceWaitIdle(m_device->GetDevice());
        }
    }

    RHI::Vulkan::CommandPoolManager* CoreContext::GetCommandPoolManager() {
        if (!m_commandPoolManager) {
            m_commandPoolManager = std::make_unique<RHI::Vulkan::CommandPoolManager>(m_device);
        }
        return m_commandPoolManager.get();
    }

    RHI::Vulkan::Device* CoreContext::GetDevice() {
        return m_device;
    }
}