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
#include <iomanip>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <set>
#include <map>
#include <variant>
#include <deque>

#include <filesystem> 

#include <volk.h>
#include <external/GLFW/glfw3.h>
#include <vk_mem_alloc.h>


#define _USE_MATH_DEFINES
#include <cmath>

#include <numbers>

#include <array>
#include <limits>
#include <stdexcept>
#include <execution>

#include <queue>
#include <mutex>
#include <condition_variable>
#include <future>
#include <atomic>

#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
