// engine/rhi/vulkan/shader_manager.cpp
#include "shader_manager.h"
#include "device.h"
#include <fstream>
#include <sstream>
#include <shaderc/shaderc.hpp>  // Assume we have shaderc for GLSL compilation

namespace RHI::Vulkan {

// ShaderProgram implementation
ShaderProgram::ShaderProgram(Device* device, const std::string& name)
    : m_device(device), m_name(name) {
}

ShaderProgram::~ShaderProgram() {
    for (auto& stage : m_stages) {
        if (stage.module) {
            vkDestroyShaderModule(m_device->GetDevice(), stage.module, nullptr);
        }
    }
}

void ShaderProgram::AddStage(VkShaderStageFlagBits stage, const std::string& path,
                            const std::string& entryPoint) {
    ShaderStageInfo info;
    info.stage = stage;
    info.path = path;
    info.entryPoint = entryPoint;
    
    m_stages.push_back(info);
}

bool ShaderProgram::Compile() {
    bool success = true;
    
    for (auto& stage : m_stages) {
        if (!CompileStage(stage)) {
            success = false;
        }
    }
    
    m_valid = success;
    return success;
}

bool ShaderProgram::CompileStage(ShaderStageInfo& stage) {
    // Check if file exists
    if (!std::filesystem::exists(stage.path)) {
        std::cerr << "Shader file not found: " << stage.path << std::endl;
        return false;
    }
    
    // Get file modification time
    stage.lastWriteTime = std::filesystem::last_write_time(stage.path);
    
    // Read shader source
    std::ifstream file(stage.path);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader file: " << stage.path << std::endl;
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    
    // Check if it's already SPIR-V
    if (stage.path.ends_with(".spv")) {
        // Read as binary
        file.close();
        std::ifstream binFile(stage.path, std::ios::binary | std::ios::ate);
        size_t fileSize = binFile.tellg();
        binFile.seekg(0);
        
        stage.spirv.resize(fileSize / sizeof(uint32_t));
        binFile.read(reinterpret_cast<char*>(stage.spirv.data()), fileSize);
    } else {
        // Compile GLSL to SPIR-V
        stage.spirv = CompileGLSL(source, stage.stage);
        if (stage.spirv.empty()) {
            return false;
        }
    }
    
    // Create shader module
    if (stage.module) {
        vkDestroyShaderModule(m_device->GetDevice(), stage.module, nullptr);
    }
    
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = stage.spirv.size() * sizeof(uint32_t);
    moduleInfo.pCode = stage.spirv.data();
    
    VK_CHECK(vkCreateShaderModule(m_device->GetDevice(), &moduleInfo, nullptr, &stage.module));
    
    return true;
}

std::vector<uint32_t> ShaderProgram::CompileGLSL(const std::string& source, VkShaderStageFlagBits stage) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    
    // Set target environment
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    options.SetTargetSpirv(shaderc_spirv_version_1_6);
    
    // Optimization
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    
    // Determine shader kind
    shaderc_shader_kind kind;
    switch (stage) {
        case VK_SHADER_STAGE_VERTEX_BIT:
            kind = shaderc_glsl_vertex_shader;
            break;
        case VK_SHADER_STAGE_FRAGMENT_BIT:
            kind = shaderc_glsl_fragment_shader;
            break;
        case VK_SHADER_STAGE_COMPUTE_BIT:
            kind = shaderc_glsl_compute_shader;
            break;
        case VK_SHADER_STAGE_GEOMETRY_BIT:
            kind = shaderc_glsl_geometry_shader;
            break;
        case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
            kind = shaderc_glsl_tess_control_shader;
            break;
        case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
            kind = shaderc_glsl_tess_evaluation_shader;
            break;
        default:
            return {};
    }
    
    // Compile
    auto result = compiler.CompileGlslToSpv(source, kind, "shader", options);
    
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        std::cerr << "Shader compilation failed: " << result.GetErrorMessage() << std::endl;
        return {};
    }
    
    return {result.cbegin(), result.cend()};
}

bool ShaderProgram::NeedsReload() const {
    for (const auto& stage : m_stages) {
        if (!std::filesystem::exists(stage.path)) {
            continue;
        }
        
        auto currentTime = std::filesystem::last_write_time(stage.path);
        if (currentTime > stage.lastWriteTime) {
            return true;
        }
    }
    
    return false;
}

bool ShaderProgram::Reload() {
    std::cout << "Reloading shader program: " << m_name << std::endl;
    return Compile();
}

std::vector<VkPipelineShaderStageCreateInfo> ShaderProgram::GetStageCreateInfos() const {
    std::vector<VkPipelineShaderStageCreateInfo> infos;
    
    for (const auto& stage : m_stages) {
        if (!stage.module) continue;
        
        VkPipelineShaderStageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        info.stage = stage.stage;
        info.module = stage.module;
        info.pName = stage.entryPoint.c_str();
        
        infos.push_back(info);
    }
    
    return infos;
}

// ShaderManager implementation
ShaderManager::ShaderManager(Device* device) : m_device(device) {
}

ShaderManager::~ShaderManager() {
    // Stop watcher thread
    if (m_watcherRunning) {
        m_watcherRunning = false;
        if (m_watcherThread.joinable()) {
            m_watcherThread.join();
        }
    }
}

ShaderProgram* ShaderManager::CreateProgram(const std::string& name) {
    auto program = std::make_unique<ShaderProgram>(m_device, name);
    ShaderProgram* ptr = program.get();
    m_programs[name] = std::move(program);
    return ptr;
}

ShaderProgram* ShaderManager::GetProgram(const std::string& name) {
    auto it = m_programs.find(name);
    return it != m_programs.end() ? it->second.get() : nullptr;
}

void ShaderManager::RemoveProgram(const std::string& name) {
    m_programs.erase(name);
}

void ShaderManager::EnableHotReload(bool enable) {
    if (enable && !m_hotReloadEnabled) {
        m_hotReloadEnabled = true;
        m_watcherRunning = true;
        m_watcherThread = std::thread(&ShaderManager::WatcherThread, this);
    } else if (!enable && m_hotReloadEnabled) {
        m_hotReloadEnabled = false;
        m_watcherRunning = false;
        if (m_watcherThread.joinable()) {
            m_watcherThread.join();
        }
    }
}

void ShaderManager::CheckForReloads() {
    std::lock_guard lock(m_reloadMutex);
    
    for (auto& [name, program] : m_programs) {
        if (program->NeedsReload()) {
            if (program->Reload()) {
                std::cout << "Shader program '" << name << "' reloaded successfully" << std::endl;
                
                // Notify callback
                if (m_reloadCallback) {
                    m_reloadCallback(name);
                }
            } else {
                std::cerr << "Failed to reload shader program: " << name << std::endl;
            }
        }
    }
}

void ShaderManager::WatcherThread() {
    while (m_watcherRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        if (m_hotReloadEnabled) {
            CheckForReloads();
        }
    }
}

void ShaderManager::AddIncludePath(const std::string& path) {
    m_includePaths.push_back(path);
}

void ShaderManager::SetCachePath(const std::string& path) {
    m_cachePath = path;
    std::filesystem::create_directories(path);
}

// ReloadablePipeline implementation
ReloadablePipeline::ReloadablePipeline(Device* device, ShaderManager* shaderManager)
    : m_device(device), m_shaderManager(shaderManager) {
}

ReloadablePipeline::~ReloadablePipeline() {
    Destroy();
}

bool ReloadablePipeline::Create(const CreateInfo& info) {
    m_createInfo = info;
    
    // Get shader program
    m_shaderProgram = m_shaderManager->GetProgram(info.shaderProgram);
    if (!m_shaderProgram) {
        std::cerr << "Shader program not found: " << info.shaderProgram << std::endl;
        return false;
    }
    
    // Create pipeline
    return CreatePipeline();
}

bool ReloadablePipeline::CreatePipeline() {
    // Destroy old pipeline
    if (m_pipeline) {
        vkDestroyPipeline(m_device->GetDevice(), m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    
    // Create pipeline layout
    if (!m_pipelineLayout) {
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = m_createInfo.pushConstantStages;
        pushConstantRange.offset = 0;
        pushConstantRange.size = m_createInfo.pushConstantSize;
        
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = m_createInfo.descriptorLayoutCount;
        layoutInfo.pSetLayouts = m_createInfo.descriptorLayouts;
        
        if (m_createInfo.pushConstantSize > 0) {
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushConstantRange;
        }
        
        VK_CHECK(vkCreatePipelineLayout(m_device->GetDevice(), &layoutInfo, nullptr, &m_pipelineLayout));
    }
    
    // Get shader stages
    auto stages = m_shaderProgram->GetStageCreateInfos();
    
    // Vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(m_createInfo.vertexBindings.size());
    vertexInputInfo.pVertexBindingDescriptions = m_createInfo.vertexBindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_createInfo.vertexAttributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = m_createInfo.vertexAttributes.data();
    
    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = m_createInfo.topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    // Viewport state
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = m_createInfo.polygonMode;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = m_createInfo.cullMode;
    rasterizer.frontFace = m_createInfo.frontFace;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    // Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = m_createInfo.depthTestEnable;
    depthStencil.depthWriteEnable = m_createInfo.depthWriteEnable;
    depthStencil.depthCompareOp = m_createInfo.depthCompareOp;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    
    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = m_createInfo.blendEnable;
    colorBlendAttachment.srcColorBlendFactor = m_createInfo.srcColorBlendFactor;
    colorBlendAttachment.dstColorBlendFactor = m_createInfo.dstColorBlendFactor;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    // Dynamic state
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    
    // Dynamic rendering
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &m_createInfo.colorFormat;
    renderingInfo.depthAttachmentFormat = m_createInfo.depthFormat;
    
    // Create pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = m_createInfo.depthTestEnable ? &depthStencil : nullptr;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    
    VkResult result = vkCreateGraphicsPipelines(m_device->GetDevice(), VK_NULL_HANDLE,
                                               1, &pipelineInfo, nullptr, &m_pipeline);
    
    return result == VK_SUCCESS;
}

void ReloadablePipeline::Destroy() {
    if (m_pipeline) {
        vkDestroyPipeline(m_device->GetDevice(), m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    
    if (m_pipelineLayout) {
        vkDestroyPipelineLayout(m_device->GetDevice(), m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
}

void ReloadablePipeline::Bind(VkCommandBuffer cmd) {
    if (m_pipeline) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    }
}

bool ReloadablePipeline::Reload() {
    if (!m_shaderProgram) return false;
    
    std::cout << "Reloading pipeline for shader: " << m_createInfo.shaderProgram << std::endl;
    
    if (m_shaderProgram->Reload()) {
        return CreatePipeline();
    }
    
    return false;
}

} // namespace RHI::Vulkan