cmake_minimum_required(VERSION 3.20)
project(VulkanSandbox VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Определения для Windows
if(WIN32)
    add_compile_definitions(
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# Исходники
set(SRC
    main.cpp
    meadow_app.cpp
    pch.cpp
    core/application.cpp
    core/input.cpp
    core/job_system.cpp
    core/time_utils.cpp
    core/window.cpp
    physics/collider.cpp
    renderer/material_system.cpp
    renderer/shadow_mapper.cpp
    renderer/sky_renderer.cpp
    rhi/vulkan/async_resource_loader.cpp
    rhi/vulkan/command_buffer_optimizer.cpp
    rhi/vulkan/command_pool.cpp
    rhi/vulkan/descriptor_allocator.cpp
    rhi/vulkan/device.cpp
    rhi/vulkan/pipeline.cpp
    rhi/vulkan/resource.cpp
    rhi/vulkan/shader_manager.cpp
    rhi/vulkan/staging_buffer_pool.cpp
    rhi/vulkan/swapchain.cpp
    world/fps_player.cpp
    world/meadow_scene.cpp
    external/volk_impl.cpp
    external/vma_impl.cpp
)

add_executable(VulkanSandbox ${SRC})

# PCH
target_precompile_headers(VulkanSandbox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.h)
target_sources(VulkanSandbox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.h)

# Include dirs
target_include_directories(VulkanSandbox
    BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer
    ${CMAKE_CURRENT_SOURCE_DIR}/rhi
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/world
    ${CMAKE_CURRENT_SOURCE_DIR}/scene
    ${CMAKE_SOURCE_DIR}/external/stb
    ${CMAKE_SOURCE_DIR}/external/glm
    ${CMAKE_SOURCE_DIR}/external/glfw/include
)

# Vulkan SDK
if(NOT DEFINED ENV{VULKAN_SDK})
    message(FATAL_ERROR "Переменная окружения VULKAN_SDK не установлена")
endif()
find_package(Vulkan REQUIRED)
target_include_directories(VulkanSandbox SYSTEM PRIVATE
    $ENV{VULKAN_SDK}/Include
    $ENV{VULKAN_SDK}/Include/Volk
    $ENV{VULKAN_SDK}/Include/vma
)

# Link dirs
target_link_directories(VulkanSandbox PRIVATE
    ${CMAKE_SOURCE_DIR}/external/glfw/lib
)
target_link_libraries(VulkanSandbox PRIVATE Vulkan::Vulkan glfw3 user32 gdi32 shell32)

# Компиляторные опции Debug / Release (разделяем каждую опцию)
if(MSVC)
    # Debug
    target_compile_options(VulkanSandbox PRIVATE
        $<$<CONFIG:Debug>:/MDd>
        $<$<CONFIG:Debug>:/Zi>
        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Debug>:/RTC1>
    )
    target_link_libraries(VulkanSandbox PRIVATE
        $<$<CONFIG:Debug>:$ENV{VULKAN_SDK}/Lib/shaderc_combinedd.lib>
    )

    # Release
    target_compile_options(VulkanSandbox PRIVATE
        $<$<CONFIG:Release>:/MD>
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/DNDEBUG>
    )
    target_link_libraries(VulkanSandbox PRIVATE
        $<$<CONFIG:Release>:$ENV{VULKAN_SDK}/Lib/shaderc_combined.lib>
    )
endif()

# Копирование шейдеров
file(GLOB SHADERS "${CMAKE_SOURCE_DIR}/shaders/*")
foreach(shader ${SHADERS})
    configure_file(${shader} ${CMAKE_BINARY_DIR}/shaders COPYONLY)
endforeach()

# Общие предупреждения компилятора
if(MSVC)
    target_compile_options(VulkanSandbox PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(VulkanSandbox PRIVATE -Wall -Wextra -Wpedantic)
endif()
