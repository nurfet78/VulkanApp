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
    core/core_context.cpp
    core/hdri_converter.cpp
    physics/collider.cpp
    renderer/material_system.cpp
    renderer/shadow_mapper.cpp
    renderer/sky_renderer.cpp
    renderer/triangle_renderer.cpp
    renderer/CubeRenderer.cpp
    renderer/skybox_renderer.cpp
    rhi/vulkan/async_resource_loader.cpp
    rhi/vulkan/command_buffer_optimizer.cpp
    rhi/vulkan/command_pool.cpp
    rhi/vulkan/descriptor_allocator.cpp
    rhi/vulkan/device.cpp
    rhi/vulkan/resource.cpp
    rhi/vulkan/shader_manager.cpp
    rhi/vulkan/staging_buffer_pool.cpp
    rhi/vulkan/swapchain.cpp
    rhi/vulkan/compute_pipeline.cpp
    world/fps_player.cpp
    world/meadow_scene.cpp
    external/volk_impl.cpp
    external/vma_impl.cpp
)

add_executable(VulkanSandbox ${SRC})

# Устанавливаем рабочую директорию при отладке в Visual Studio
set_target_properties(VulkanSandbox PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:VulkanSandbox>"
)

# PCH
target_precompile_headers(VulkanSandbox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.h)

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

# --- НАЧАЛО БЛОКА КОПИРОВАНИЯ АССЕТОВ (ФИНАЛЬНАЯ РАБОЧАЯ ВЕРСИЯ) ---

set(SOURCE_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")

# --- 1. Явное создание папок для текстур и моделей ---
# $<TARGET_FILE_DIR:...> это правильный "генератор выражений" для получения пути вывода.
# CMake заменит его на C:/.../build/Debug или C:/.../build/Release во время сборки.
add_custom_command(
    TARGET VulkanSandbox PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:VulkanSandbox>/assets/textures"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:VulkanSandbox>/assets/models"
    COMMENT "Creating textures and models output directories"
)

# --- 2. Копирование текстур и моделей через сгенерированный скрипт ---
# Генерируем .bat скрипт из нашего шаблона.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/copy_assets.bat.in"
    "${CMAKE_BINARY_DIR}/copy_assets.bat"
    @ONLY
)

# Вызываем наш скрипт и передаем ему имя конфигурации с помощью $<CONFIG>
# $<CONFIG> - это генератор выражений, который превратится в "Debug" или "Release".
add_custom_command(
    TARGET VulkanSandbox PRE_BUILD
    COMMAND "${CMAKE_BINARY_DIR}/copy_assets.bat" "$<CONFIG>"
    COMMENT "Syncing textures and models"
    VERBATIM
)

# --- КОНЕЦ БЛОКА КОПИРОВАНИЯ АССЕТОВ ---

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT VulkanSandbox)

# Общие предупреждения компилятора
if(MSVC)
    target_compile_options(VulkanSandbox PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(VulkanSandbox PRIVATE -Wall -Wextra -Wpedantic)
endif()