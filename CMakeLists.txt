cmake_minimum_required(VERSION 3.20)
project(VulkanSandbox LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SRC
    main.cpp
    meadow_app.cpp
    core/application.cpp
    core/input.cpp
    core/job_system.cpp
    core/time.cpp
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
)

add_executable(VulkanSandbox ${SRC})

find_package(Vulkan REQUIRED)
target_link_libraries(VulkanSandbox PRIVATE ${Vulkan_LIBRARIES})
target_include_directories(VulkanSandbox PRIVATE
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/renderer
    ${CMAKE_SOURCE_DIR}/rhi
    ${CMAKE_SOURCE_DIR}/physics
    ${CMAKE_SOURCE_DIR}/world
)

if (MSVC)
    target_compile_options(VulkanSandbox PRIVATE /W4 /permissive-)
else()
    target_compile_options(VulkanSandbox PRIVATE -Wall -Wextra -Wpedantic)
endif()

