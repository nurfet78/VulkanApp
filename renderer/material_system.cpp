// engine/renderer/material_system.cpp
#include "material_system.h"
#include "rhi/vulkan/resource.h"
#include "rhi/vulkan/descriptor_allocator.h"

namespace Renderer {

// MaterialTemplate implementation
MaterialTemplate::MaterialTemplate(const std::string& name) : m_name(name) {
}

MaterialTemplate::~MaterialTemplate() {
    // Cleanup handled by device
}

void MaterialTemplate::AddParameter(const std::string& name, VkShaderStageFlags stages,
                                   uint32_t offset, uint32_t size, VkFormat format) {
    ParameterInfo param;
    param.name = name;
    param.stages = stages;
    param.offset = offset;
    param.size = size;
    param.format = format;
    
    m_parameters.push_back(param);
    m_uniformBufferSize = std::max(m_uniformBufferSize, offset + size);
}

void MaterialTemplate::AddTexture(const std::string& name, uint32_t binding,
                                 VkDescriptorType type, VkShaderStageFlags stages) {
    TextureBinding texture;
    texture.name = name;
    texture.binding = binding;
    texture.type = type;
    texture.stages = stages;
    
    m_textureBindings.push_back(texture);
}

// Material implementation
Material::Material(MaterialTemplate* template_, RHI::Vulkan::Device* device)
    : m_template(template_), m_device(device) {
    
    // Create uniform buffer if needed
    if (m_template->GetUniformBufferSize() > 0) {
        m_uniformBuffer = std::make_unique<RHI::Vulkan::Buffer>(
            device,
            m_template->GetUniformBufferSize(),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );
    }
}

Material::~Material() {
    // Cleanup handled by smart pointers
}

void Material::SetParameter(const std::string& name, const MaterialParameter& value) {
    m_parameters[name] = value;
    m_dirty = true;
}

MaterialParameter Material::GetParameter(const std::string& name) const {
    auto it = m_parameters.find(name);
    if (it != m_parameters.end()) {
        return it->second;
    }
    return MaterialParameter{};
}

void Material::SetTexture(const std::string& name, RHI::Vulkan::Image* texture) {
    if (texture) {
        SetTexture(name, texture->GetView(), VK_NULL_HANDLE); // Use default sampler
    }
}

void Material::SetTexture(const std::string& name, VkImageView view, VkSampler sampler) {
    m_textures[name] = {view, sampler};
    m_dirty = true;
}

void Material::UpdateUniformBuffer() {
    if (!m_uniformBuffer || !m_dirty) {
        return;
    }
    
    // Map buffer
    void* data = m_uniformBuffer->Map();
    
    // Write parameters to buffer
    for (const auto& paramInfo : m_template->GetParameters()) {
        auto it = m_parameters.find(paramInfo.name);
        if (it != m_parameters.end()) {
            std::visit([&](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                
                if constexpr (std::is_same_v<T, float>) {
                    memcpy(static_cast<uint8_t*>(data) + paramInfo.offset, &value, sizeof(float));
                } else if constexpr (std::is_same_v<T, glm::vec2>) {
                    memcpy(static_cast<uint8_t*>(data) + paramInfo.offset, &value, sizeof(glm::vec2));
                } else if constexpr (std::is_same_v<T, glm::vec3>) {
                    memcpy(static_cast<uint8_t*>(data) + paramInfo.offset, &value, sizeof(glm::vec3));
                } else if constexpr (std::is_same_v<T, glm::vec4>) {
                    memcpy(static_cast<uint8_t*>(data) + paramInfo.offset, &value, sizeof(glm::vec4));
                } else if constexpr (std::is_same_v<T, glm::mat4>) {
                    memcpy(static_cast<uint8_t*>(data) + paramInfo.offset, &value, sizeof(glm::mat4));
                } else if constexpr (std::is_same_v<T, uint32_t>) {
                    memcpy(static_cast<uint8_t*>(data) + paramInfo.offset, &value, sizeof(uint32_t));
                }
            }, it->second);
        }
    }
    
    m_uniformBuffer->Unmap();
}

void Material::UpdateDescriptorSet() {
    if (!m_descriptorSet || !m_dirty) {
        return;
    }
    
    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    
    // Uniform buffer
    if (m_uniformBuffer) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffer->GetHandle();
        bufferInfo.offset = 0;
        bufferInfo.range = m_uniformBuffer->GetSize();
        bufferInfos.push_back(bufferInfo);
        
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_descriptorSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfos.back();
        writes.push_back(write);
    }
    
    // Textures
    for (const auto& binding : m_template->GetTextureBindings()) {
        auto it = m_textures.find(binding.name);
        if (it != m_textures.end()) {
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = it->second.first;
            imageInfo.sampler = it->second.second;
            imageInfos.push_back(imageInfo);
            
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = m_descriptorSet;
            write.dstBinding = binding.binding;
            write.dstArrayElement = 0;
            write.descriptorType = binding.type;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfos.back();
            writes.push_back(write);
        }
    }
    
    if (!writes.empty()) {
        vkUpdateDescriptorSets(m_device->GetDevice(),
                             static_cast<uint32_t>(writes.size()),
                             writes.data(), 0, nullptr);
    }
}

void Material::Bind(VkCommandBuffer cmd) {
    if (m_dirty) {
        UpdateUniformBuffer();
        UpdateDescriptorSet();
        m_dirty = false;
    }
    
    if (m_descriptorSet) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              m_template->GetPipelineLayout(),
                              0, 1, &m_descriptorSet, 0, nullptr);
    }
}

// MaterialSystem implementation
MaterialSystem::MaterialSystem(RHI::Vulkan::Device* device) : m_device(device) {
    m_descriptorAllocator = std::make_unique<RHI::Vulkan::DescriptorAllocator>(device);
    m_layoutCache = std::make_unique<RHI::Vulkan::DescriptorLayoutCache>(device);
    
    CreateStandardTemplates();
    CreateDefaultMaterials();
}

MaterialSystem::~MaterialSystem() {
    m_materials.clear();
    m_templates.clear();
}

MaterialTemplate* MaterialSystem::CreateTemplate(const std::string& name) {
    auto template_ = std::make_unique<MaterialTemplate>(name);
    MaterialTemplate* ptr = template_.get();
    m_templates[name] = std::move(template_);
    return ptr;
}

MaterialTemplate* MaterialSystem::GetTemplate(const std::string& name) {
    auto it = m_templates.find(name);
    return it != m_templates.end() ? it->second.get() : nullptr;
}

Material* MaterialSystem::CreateMaterial(const std::string& name, const std::string& templateName) {
    auto template_ = GetTemplate(templateName);
    if (!template_) {
        return nullptr;
    }
    
    auto material = std::make_unique<Material>(template_, m_device);
    
    // Allocate descriptor set
    if (template_->GetDescriptorLayout()) {
        VkDescriptorSet set;
        if (m_descriptorAllocator->Allocate(&set, template_->GetDescriptorLayout())) {
            material->SetDescriptorSet(set);
        }
    }
    
    Material* ptr = material.get();
    m_materials[name] = std::move(material);
    return ptr;
}

Material* MaterialSystem::GetMaterial(const std::string& name) {
    auto it = m_materials.find(name);
    return it != m_materials.end() ? it->second.get() : nullptr;
}

void MaterialSystem::UpdateDirtyMaterials() {
    for (auto& [name, material] : m_materials) {
        if (material->IsDirty()) {
            material->UpdateUniformBuffer();
            material->UpdateDescriptorSet();
            material->ClearDirty();
        }
    }
}

void MaterialSystem::ResetDescriptorPools() {
    m_descriptorAllocator->ResetPools();
}

void MaterialSystem::CreateStandardTemplates() {
    // PBR Material Template
    auto pbr = CreateTemplate("pbr");
    pbr->AddParameter("baseColor", VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec4), VK_FORMAT_R32G32B32A32_SFLOAT);
    pbr->AddParameter("metallic", VK_SHADER_STAGE_FRAGMENT_BIT, 16, sizeof(float), VK_FORMAT_R32_SFLOAT);
    pbr->AddParameter("roughness", VK_SHADER_STAGE_FRAGMENT_BIT, 20, sizeof(float), VK_FORMAT_R32_SFLOAT);
    pbr->AddParameter("ao", VK_SHADER_STAGE_FRAGMENT_BIT, 24, sizeof(float), VK_FORMAT_R32_SFLOAT);
    pbr->AddParameter("emissive", VK_SHADER_STAGE_FRAGMENT_BIT, 28, sizeof(float), VK_FORMAT_R32_SFLOAT);
    
    pbr->AddTexture("albedo", 1);
    pbr->AddTexture("normal", 2);
    pbr->AddTexture("metallicRoughness", 3);
    pbr->AddTexture("ao", 4);
    pbr->AddTexture("emissive", 5);
    
    // Unlit Material Template
    auto unlit = CreateTemplate("unlit");
    unlit->AddParameter("color", VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec4), VK_FORMAT_R32G32B32A32_SFLOAT);
    unlit->AddTexture("texture", 1);
    
    // Terrain Material Template
    auto terrain = CreateTemplate("terrain");
    terrain->AddParameter("tiling", VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                         0, sizeof(glm::vec4), VK_FORMAT_R32G32B32A32_SFLOAT);
    terrain->AddTexture("texture0", 1);
    terrain->AddTexture("texture1", 2);
    terrain->AddTexture("texture2", 3);
    terrain->AddTexture("texture3", 4);
    terrain->AddTexture("blendMap", 5);
}

void MaterialSystem::CreateDefaultMaterials() {
    // Default PBR material
    auto defaultMat = CreateMaterial("default", "pbr");
    if (defaultMat) {
        defaultMat->SetParameter("baseColor", glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));
        defaultMat->SetParameter("metallic", 0.0f);
        defaultMat->SetParameter("roughness", 0.5f);
        defaultMat->SetParameter("ao", 1.0f);
        defaultMat->SetParameter("emissive", 0.0f);
    }
    
    // Error material (bright magenta)
    auto errorMat = CreateMaterial("error", "unlit");
    if (errorMat) {
        errorMat->SetParameter("color", glm::vec4(1.0f, 0.0f, 1.0f, 1.0f));
    }
    
    // Grass material
    auto grassMat = CreateMaterial("grass", "pbr");
    if (grassMat) {
        grassMat->SetParameter("baseColor", glm::vec4(0.2f, 0.5f, 0.1f, 1.0f));
        grassMat->SetParameter("metallic", 0.0f);
        grassMat->SetParameter("roughness", 0.8f);
        grassMat->SetParameter("ao", 1.0f);
        grassMat->SetParameter("emissive", 0.0f);
    }
    
    // Wood material
    auto woodMat = CreateMaterial("wood", "pbr");
    if (woodMat) {
        woodMat->SetParameter("baseColor", glm::vec4(0.4f, 0.25f, 0.1f, 1.0f));
        woodMat->SetParameter("metallic", 0.0f);
        woodMat->SetParameter("roughness", 0.7f);
        woodMat->SetParameter("ao", 1.0f);
        woodMat->SetParameter("emissive", 0.0f);
    }
    
    // Roof material
    auto roofMat = CreateMaterial("roof", "pbr");
    if (roofMat) {
        roofMat->SetParameter("baseColor", glm::vec4(0.5f, 0.2f, 0.1f, 1.0f));
        roofMat->SetParameter("metallic", 0.0f);
        roofMat->SetParameter("roughness", 0.9f);
        roofMat->SetParameter("ao", 1.0f);
        roofMat->SetParameter("emissive", 0.0f);
    }
}

} // namespace Renderer