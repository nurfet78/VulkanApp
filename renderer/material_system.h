// engine/renderer/material_system.h
#pragma once

#include "rhi/vulkan/vulkan_common.h"


namespace RHI::Vulkan {
    class Device;
    class Pipeline;
    class Buffer;
    class Image;
    class DescriptorAllocator;
    class DescriptorLayoutCache;
}

namespace Renderer {

// Material parameter types
using MaterialParameter = std::variant<
    float,
    glm::vec2,
    glm::vec3,
    glm::vec4,
    glm::mat4,
    uint32_t,  // Texture index for bindless
    VkImageView,
    VkSampler
>;

// Material template defines the structure
class MaterialTemplate {
public:
    struct ParameterInfo {
        std::string name;
        VkShaderStageFlags stages;
        uint32_t offset;
        uint32_t size;
        VkFormat format;
    };
    
    struct TextureBinding {
        std::string name;
        uint32_t binding;
        VkDescriptorType type;
        VkShaderStageFlags stages;
    };
    
    MaterialTemplate(const std::string& name);
    ~MaterialTemplate();
    
    // Define parameters
    void AddParameter(const std::string& name, VkShaderStageFlags stages,
                     uint32_t offset, uint32_t size, VkFormat format);
    void AddTexture(const std::string& name, uint32_t binding,
                   VkDescriptorType type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   VkShaderStageFlags stages = VK_SHADER_STAGE_FRAGMENT_BIT);
    
    // Pipeline management
    void SetPipeline(RHI::Vulkan::Pipeline* pipeline) { m_pipeline = pipeline; }
    RHI::Vulkan::Pipeline* GetPipeline() const { return m_pipeline; }
    
    VkDescriptorSetLayout GetDescriptorLayout() const { return m_descriptorLayout; }
    VkPipelineLayout GetPipelineLayout() const { return m_pipelineLayout; }
    
    const std::vector<ParameterInfo>& GetParameters() const { return m_parameters; }
    const std::vector<TextureBinding>& GetTextureBindings() const { return m_textureBindings; }
    
    uint32_t GetUniformBufferSize() const { return m_uniformBufferSize; }
    
private:
    std::string m_name;
    RHI::Vulkan::Pipeline* m_pipeline = nullptr;
    
    std::vector<ParameterInfo> m_parameters;
    std::vector<TextureBinding> m_textureBindings;
    
    VkDescriptorSetLayout m_descriptorLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    
    uint32_t m_uniformBufferSize = 0;
};

// Material instance
class Material {
public:
    Material(MaterialTemplate* template_, RHI::Vulkan::Device* device);
    ~Material();
    
    // Set parameters
    void SetParameter(const std::string& name, const MaterialParameter& value);
    MaterialParameter GetParameter(const std::string& name) const;
    
    // Set textures
    void SetTexture(const std::string& name, RHI::Vulkan::Image* texture);
    void SetTexture(const std::string& name, VkImageView view, VkSampler sampler);
    
    // Update GPU resources
    void UpdateUniformBuffer();
    void UpdateDescriptorSet();
    
    // Bind for rendering
    void Bind(VkCommandBuffer cmd);
    
    // Get resources
    VkDescriptorSet GetDescriptorSet() const { return m_descriptorSet; }
    RHI::Vulkan::Buffer* GetUniformBuffer() const { return m_uniformBuffer.get(); }
    MaterialTemplate* GetTemplate() const { return m_template; }
    
    bool IsDirty() const { return m_dirty; }
    void ClearDirty() { m_dirty = false; }
    
private:
    MaterialTemplate* m_template;
    RHI::Vulkan::Device* m_device;
    
    // Parameters
    std::unordered_map<std::string, MaterialParameter> m_parameters;
    std::unordered_map<std::string, std::pair<VkImageView, VkSampler>> m_textures;
    
    // GPU resources
    std::unique_ptr<RHI::Vulkan::Buffer> m_uniformBuffer;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    
    bool m_dirty = true;
};

// Material system manager
class MaterialSystem {
public:
    MaterialSystem(RHI::Vulkan::Device* device);
    ~MaterialSystem();
    
    // Template management
    MaterialTemplate* CreateTemplate(const std::string& name);
    MaterialTemplate* GetTemplate(const std::string& name);
    
    // Material management
    Material* CreateMaterial(const std::string& name, const std::string& templateName);
    Material* GetMaterial(const std::string& name);
    
    // Batch update all dirty materials
    void UpdateDirtyMaterials();
    
    // Descriptor management
    void ResetDescriptorPools();
    
    // Default materials
    void CreateDefaultMaterials();
    Material* GetDefaultMaterial() { return GetMaterial("default"); }
    Material* GetErrorMaterial() { return GetMaterial("error"); }
    
private:
    void CreateStandardTemplates();
    
    RHI::Vulkan::Device* m_device;
    
    std::unique_ptr<RHI::Vulkan::DescriptorAllocator> m_descriptorAllocator;
    std::unique_ptr<RHI::Vulkan::DescriptorLayoutCache> m_layoutCache;
    
    std::unordered_map<std::string, std::unique_ptr<MaterialTemplate>> m_templates;
    std::unordered_map<std::string, std::unique_ptr<Material>> m_materials;
};