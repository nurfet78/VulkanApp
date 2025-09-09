// engine/rhi/vulkan/shader_manager.cpp
#include "shader_manager.h"
#include "device.h"
#include <shaderc/shaderc.hpp>  // Assume we have shaderc for GLSL compilation

namespace RHI::Vulkan {

    //==============================================================================
    // ShaderProgram
    //==============================================================================

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
        m_stages.emplace_back(ShaderStageInfo{
            .stage = stage,
            .path = path,
            .entryPoint = entryPoint
            });
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
        // Убеждаемся, что нам передали .spv файл.
        if (!stage.path.ends_with(".spv")) {
            std::cerr << "Error: CompileStage now only accepts precompiled .spv files. Received: " << stage.path << std::endl;
            return false;
        }

        if (!std::filesystem::exists(stage.path)) {
            std::cerr << "Shader file not found: " << stage.path << std::endl;
            return false;
        }

        // Читаем бинарный SPIR-V файл
        std::ifstream file(stage.path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open shader file: " << stage.path << std::endl;
            return false;
        }

        size_t fileSize = static_cast<size_t>(file.tellg());
        if (fileSize % sizeof(uint32_t) != 0) {
            std::cerr << "Invalid SPIR-V file size: " << stage.path << std::endl;
            return false;
        }

        stage.spirv.resize(fileSize / sizeof(uint32_t));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(stage.spirv.data()), fileSize);
        file.close();

        if (stage.spirv.empty()) {
            std::cerr << "Failed to read SPIR-V code from: " << stage.path << std::endl;
            return false;
        }

        // Уничтожаем старый модуль, если он был (нужно для ReloadablePipeline)
        if (stage.module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device->GetDevice(), stage.module, nullptr);
        }

        // Создаем VkShaderModule из загруженного кода
        VkShaderModuleCreateInfo moduleInfo{};
        moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleInfo.codeSize = stage.spirv.size() * sizeof(uint32_t);
        moduleInfo.pCode = stage.spirv.data();

        if (vkCreateShaderModule(m_device->GetDevice(), &moduleInfo, nullptr, &stage.module) != VK_SUCCESS) {
            std::cerr << "Failed to create shader module for " << stage.path << std::endl;
            return false;
        }

        return true;
    }

    std::vector<uint32_t> ShaderProgram::CompileGLSL(const std::string& source, VkShaderStageFlagBits stage, const std::string& sourceName) {
        shaderc::Compiler compiler;
        shaderc::CompileOptions options;

        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
        options.SetTargetSpirv(shaderc_spirv_version_1_6);

        // --- ГЛАВНОЕ ИСПРАВЛЕНИЕ ---
        // Явно включаем генерацию отладочной информации.
        // Это заставляет shaderc использовать более стабильный кодовый путь
        // для генерации инструкций OpName и решает проблему с валидацией.
        options.SetGenerateDebugInfo();

        // Режим оптимизации можно менять (performance или size)
        options.SetOptimizationLevel(shaderc_optimization_level_performance);

        shaderc_shader_kind kind;
        switch (stage) {
        case VK_SHADER_STAGE_VERTEX_BIT:                   kind = shaderc_glsl_vertex_shader; break;
        case VK_SHADER_STAGE_FRAGMENT_BIT:                 kind = shaderc_glsl_fragment_shader; break;
        case VK_SHADER_STAGE_COMPUTE_BIT:                  kind = shaderc_glsl_compute_shader; break;
        case VK_SHADER_STAGE_GEOMETRY_BIT:                 kind = shaderc_glsl_geometry_shader; break;
        case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:     kind = shaderc_glsl_tess_control_shader; break;
        case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:  kind = shaderc_glsl_tess_evaluation_shader; break;
        default:
            std::cerr << "Unsupported shader stage for compilation." << std::endl;
            return {};
        }

        // Передаем настоящее имя файла для более информативных ошибок
        shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(source, kind, sourceName.c_str(), options);

        if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
            std::cerr << "Shader compilation failed for " << sourceName << ":\n" << result.GetErrorMessage() << std::endl;
            return {};
        }

        return { result.cbegin(), result.cend() };
    }


    std::vector<VkPipelineShaderStageCreateInfo> ShaderProgram::GetStageCreateInfos() const {
        std::vector<VkPipelineShaderStageCreateInfo> infos;
        infos.reserve(m_stages.size());

        for (const auto& stage : m_stages) {
            if (stage.module == VK_NULL_HANDLE) continue;

            VkPipelineShaderStageCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            info.stage = stage.stage;
            info.module = stage.module;
            info.pName = stage.entryPoint.c_str();

            infos.push_back(info);
        }
        return infos;
    }


    //==============================================================================
    // ShaderManager
    //==============================================================================

    ShaderManager::ShaderManager(Device* device) : m_device(device) {}

    ShaderManager::~ShaderManager() {
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
        // Уничтожаем старый пайплайн, если он существует. Это нужно для перезагрузки.
        if (m_pipeline) {
            vkDestroyPipeline(m_device->GetDevice(), m_pipeline, nullptr);
            m_pipeline = VK_NULL_HANDLE;
        }

        // Создаем pipeline layout, если он еще не создан.
        // Layout зависит от дескрипторов и push-констант, и обычно не меняется при перезагрузке шейдеров.
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

            if (vkCreatePipelineLayout(m_device->GetDevice(), &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
                std::cerr << "Failed to create pipeline layout for shader: " << m_shaderProgram->GetName() << std::endl;
                return false;
            }
        }

        // Получаем скомпилированные стадии шейдеров из ShaderProgram
        auto stages = m_shaderProgram->GetStageCreateInfos();
        if (stages.empty()) {
            std::cerr << "Shader program has no valid stages: " << m_shaderProgram->GetName() << std::endl;
            return false;
        }

        // --- НАСТРОЙКА СОСТОЯНИЙ ПАЙПЛАЙНА ---

        // 1. Входные данные вершин (Vertex Input)
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(m_createInfo.vertexBindings.size());
        vertexInputInfo.pVertexBindingDescriptions = m_createInfo.vertexBindings.data();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_createInfo.vertexAttributes.size());
        vertexInputInfo.pVertexAttributeDescriptions = m_createInfo.vertexAttributes.data();

        // 2. Сборка примитивов (Input Assembly)
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = m_createInfo.topology;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // 3. Viewport и Scissor (будут динамическими)
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        // 4. Растеризация (Rasterizer)
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = m_createInfo.polygonMode;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = m_createInfo.cullMode;
        rasterizer.frontFace = m_createInfo.frontFace;
        rasterizer.depthBiasEnable = VK_FALSE;

        // 5. Мультисэмплинг (Multisampling)
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // 6. Тест глубины и трафарета (Depth/Stencil)
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = m_createInfo.depthTestEnable;
        depthStencil.depthWriteEnable = m_createInfo.depthWriteEnable;
        depthStencil.depthCompareOp = m_createInfo.depthCompareOp;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        // 7. Смешивание цветов (Color Blending)
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

        // 8. Динамические состояния (Dynamic State)
        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        // 9. Информация о рендеринге (Dynamic Rendering)
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &m_createInfo.colorFormat;
        renderingInfo.depthAttachmentFormat = m_createInfo.depthFormat;
        // stencilAttachmentFormat остается по умолчанию VK_FORMAT_UNDEFINED

        // --- СБОРКА И СОЗДАНИЕ ПАЙПЛАЙНА ---

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &renderingInfo; // Подключаем информацию о Dynamic Rendering
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_pipelineLayout;
        pipelineInfo.renderPass = VK_NULL_HANDLE; // renderPass не используется с Dynamic Rendering

        // ================== ИСПРАВЛЕНИЕ ЗДЕСЬ ==================
        // Слой валидации требует, чтобы pDepthStencilState был предоставлен,
        // если мы указали формат для depthAttachmentFormat, даже если все тесты выключены.
        if (m_createInfo.depthFormat != VK_FORMAT_UNDEFINED) {
            pipelineInfo.pDepthStencilState = &depthStencil;
        }
        else {
            pipelineInfo.pDepthStencilState = nullptr;
        }
        // =======================================================

        VkResult result = vkCreateGraphicsPipelines(m_device->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);

        if (result != VK_SUCCESS) {
            std::cerr << "Failed to create graphics pipeline for shader: " << m_shaderProgram->GetName() << std::endl;
            return false;
        }

        return true;
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

} // namespace RHI::Vulkan