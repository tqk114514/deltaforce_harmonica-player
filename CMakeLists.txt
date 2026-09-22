cmake_minimum_required(VERSION 3.22)
project(harmonica-player VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
  message(FATAL_ERROR "本项目只支持 MSVC：输入层用的是 Windows 的 SendInput。")
endif()

option(HARMONICA_BUILD_TESTS "编译 core 的 QTest 套件" ON)

# Qt：本机在 D:/Qt/6.11.2/msvc2022_64，命令行构建时用 -DCMAKE_PREFIX_PATH 指过来
find_package(Qt6 REQUIRED COMPONENTS Core)
if(HARMONICA_BUILD_TESTS)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  enable_testing()
endif()

add_subdirectory(core)
