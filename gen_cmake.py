#!/usr/bin/env python3
import os
from pathlib import Path

PROJECT = "VulkanSandbox"
CPP_STD = "20"

root = Path(".").resolve()

def collect_sources():
    sources = []
    for p in root.rglob("*.cpp"):
        # Игнорируем build/ и скрытые каталоги
        if "build" in p.parts or p.name.startswith("."):
            continue
        sources.append(p.relative_to(root))
    return sources

def make_cmake(sources):
    src_list = "\n    ".join(str(s) for s in sources)
    return f"""cmake_minimum_required(VERSION 3.20)
project({PROJECT} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD {CPP_STD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Список исходников
set(SRC
    {src_list}
)

add_executable({PROJECT} ${{SRC}})

# Vulkan SDK (если стоит в системе)
find_package(Vulkan REQUIRED)
target_include_directories({PROJECT} PRIVATE ${{Vulkan_INCLUDE_DIRS}})
target_link_libraries({PROJECT} PRIVATE ${{Vulkan_LIBRARIES}})

# Добавляем корневые include
target_include_directories({PROJECT} PRIVATE
    ${{CMAKE_SOURCE_DIR}}
)
"""

if __name__ == "__main__":
    srcs = collect_sources()
    if not srcs:
        print("*.cpp не найдено")
    else:
        cmake_txt = make_cmake(srcs)
        Path("CMakeLists.txt").write_text(cmake_txt, encoding="utf-8")
        print(f"CMakeLists.txt создан, найдено {len(srcs)} cpp файлов.")
