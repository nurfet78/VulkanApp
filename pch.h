// engine/pch.h
#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN


#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>


#include <chrono>
#include <thread>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <set>

#include <volk.h>

// Дальше GLFW – но без макроса GLFW_INCLUDE_VULKAN!
// GLFW всё равно увидит типы VkInstance, VkSurfaceKHR,
// потому что volk уже подтянул vulkan.h
#include <GLFW/glfw3.h>

// Теперь любые дополнительные заголовки
#include <vk_mem_alloc.h>

//#include <GLFW/glfw3.h>

//#define VK_NO_PROTOTYPES
//#include <vulkan/vulkan.h>
//#include <volk.h>
//#include <vk_mem_alloc.h>

#define _USE_MATH_DEFINES
#include <cmath>

#include <array>
#include <limits>
#include <stdexcept>

#include <queue>
#include <mutex>
#include <condition_variable>
#include <future>
#include <atomic>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
