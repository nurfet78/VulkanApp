// engine/rhi/vulkan/shader_manager.h
#pragma once

#include "vulkan_common.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <chrono>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

namespace RHI::Vulkan {

class Device;
class Pipeline;

// Shader stage info
struct ShaderStageInfo {
    VkShaderStageFlagBits stage;
    std::string path;
    std::string entryPoint = "main";
    std::vector<uint32_t> spirv;
    VkShaderModule module = VK_NULL_HANDLE;
    std::filesystem::file_time_type lastWriteTime;
};

// Shader program (collection of stages)
class ShaderProgram {
public:
    ShaderProgram(Device* device, const std::string& name);
    ~ShaderProgram();
    
    // Add shader stages
    void AddStage(VkShaderStageFlagBits stage, const std::string& path,
                 const std::string& entryPoint = "main");
    
    // Compile all stages
    bool Compile();
    
    // Get compiled modules
    const std::vector<ShaderStageInfo>& GetStages() const { return m_stages; }
    std::vector<VkPipelineShaderStageCreateInfo> GetStageCreateInfos() const;
    
    // Check if needs recompilation
    bool NeedsReload() const;
    
    // Reload shaders
    bool Reload();
    
    const std::string& GetName() const { return m_name; }
    bool IsValid() const { return m_valid; }
    
private:
    bool CompileStage(ShaderStageInfo& stage);
    std::vector<uint32_t> CompileGLSL(const std::string& source, VkShaderStageFlagBits stage);
    
    Device* m_device;
    std::string m_name;
    std::vector<ShaderStageInfo> m_stages;
    bool m_valid = false;
};

// Shader manager with hot-reload support
class ShaderManager {
public:
    ShaderManager(Device* device);
    ~ShaderManager();
    
    // Shader management
    ShaderProgram* CreateProgram(const std::string& name);
    ShaderProgram* GetProgram(const std::string& name);
    void RemoveProgram(const std::string& name);
    
    // Hot reload
    void EnableHotReload(bool enable);
    bool IsHotReloadEnabled() const { return m_hotReloadEnabled; }
    
    // Set reload callback
    using ReloadCallback = std::function<void(const std::string& programName)>;
    void SetReloadCallback(ReloadCallback callback) { m_reloadCallback = callback; }
    
    // Manual reload check
    void CheckForReloads();
    
    // Include directories for shader compilation
    void AddIncludePath(const std::string& path);
    
    // Shader cache
    void SetCachePath(const std::string& path);
    bool LoadFromCache(const std::string& name);
    void SaveToCache(const std::string& name);
    
private:
    void WatcherThread();
    void ProcessFileChange(const std::string& path);
    
    Device* m_device;
    
    std::unordered_map<std::string, std::unique_ptr<ShaderProgram>> m_programs;
    std::unordered_map<std::string, std::vector<std::string>> m_fileToProgramMap;
    
    // Hot reload
    std::atomic<bool> m_hotReloadEnabled{false};
    std::thread m_watcherThread;
    std::atomic<bool> m_watcherRunning{false};
    std::mutex m_reloadMutex;
    ReloadCallback m_reloadCallback;
    
    // Shader compilation
    std::vector<std::string> m_includePaths;
    std::string m_cachePath;
};

// Pipeline with automatic shader reload
class ReloadablePipeline {
public:
    ReloadablePipeline(Device* device, ShaderManager* shaderManager);
    ~ReloadablePipeline();
    
    struct CreateInfo {
        std::string shaderProgram;
        
        // Vertex input
        std::vector<VkVertexInputBindingDescription> vertexBindings;
        std::vector<VkVertexInputAttributeDescription> vertexAttributes;
        
        // Pipeline state
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
        VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
        VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        
        // Depth state
        bool depthTestEnable = true;
        bool depthWriteEnable = true;
        VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
        
        // Blend state
        bool blendEnable = false;
        VkBlendFactor srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        VkBlendFactor dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        
        // Render state
        VkFormat colorFormat = VK_FORMAT_B8G8R8A8_SRGB;
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        
        // Layout
        VkDescriptorSetLayout* descriptorLayouts = nullptr;
        uint32_t descriptorLayoutCount = 0;
        uint32_t pushConstantSize = 0;
        VkShaderStageFlags pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;
    };
    
    bool Create(const CreateInfo& info);
    void Destroy();
    
    // Bind pipeline
    void Bind(VkCommandBuffer cmd);
    
    // Get handles
    VkPipeline GetPipeline() const { return m_pipeline; }
    VkPipelineLayout GetLayout() const { return m_pipelineLayout; }
    
    // Force reload
    bool Reload();
    
private:
    bool CreatePipeline();
    
    Device* m_device;
    ShaderManager* m_shaderManager;
    
    CreateInfo m_createInfo;
    ShaderProgram* m_shaderProgram = nullptr;
    
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
};