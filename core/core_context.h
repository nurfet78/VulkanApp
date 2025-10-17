#pragma once

#include "rhi/vulkan/command_pool.h"

namespace RHI::Vulkan {
    class Device;
}

namespace Core {

    class CoreContext {
    public:
        explicit CoreContext(RHI::Vulkan::Device* device);
        CoreContext(const CoreContext&) = delete;
        CoreContext& operator=(const CoreContext&) = delete;
        CoreContext(CoreContext&&) = default;
        CoreContext& operator=(CoreContext&&) = default;
        ~CoreContext();

        RHI::Vulkan::CommandPoolManager* GetCommandPoolManager();
        RHI::Vulkan::Device* GetDevice();

    private:
        RHI::Vulkan::Device* m_device = nullptr;
        mutable std::unique_ptr<RHI::Vulkan::CommandPoolManager> m_commandPoolManager; // mutable для ленивой инициализации
    };
}
