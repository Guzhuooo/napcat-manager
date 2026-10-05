cmake_minimum_required(VERSION 3.10)

# ============================================================================
#  NapCat 管理器 native 扩展
#
#  目标平台：有道词典笔（Rockchip RK3562 / aarch64 / glibc 2.36）
#  交叉工具链由环境变量提供；本地不编译，全部在 GitHub Actions 上进行。
#
#    CROSS_TOOLCHAIN_PREFIX 例：aarch64-linux-gnu-
#    或分别指定 CROSS_C_COMPILER / CROSS_CXX_COMPILER
# ============================================================================

if(DEFINED ENV{CROSS_C_COMPILER} AND DEFINED ENV{CROSS_CXX_COMPILER})
  set(CMAKE_C_COMPILER "$ENV{CROSS_C_COMPILER}")
  set(CMAKE_CXX_COMPILER "$ENV{CROSS_CXX_COMPILER}")
elseif(DEFINED ENV{CROSS_TOOLCHAIN_PREFIX})
  set(CMAKE_C_COMPILER "$ENV{CROSS_TOOLCHAIN_PREFIX}gcc")
  set(CMAKE_CXX_COMPILER "$ENV{CROSS_TOOLCHAIN_PREFIX}g++")
else()
  message(FATAL_ERROR "必须在环境中提供 CROSS_TOOLCHAIN_PREFIX 或 CROSS_C/CXX_COMPILER")
endif()

project(jsapi_napcat C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 静态链接 libstdc++/libgcc，避免依赖设备侧 GLIBCXX 版本
add_link_options(-static-libstdc++ -static-libgcc)

set(SDK "${CMAKE_SOURCE_DIR}/iot-miniapp-sdk")

# ---- SDK ----
file(GLOB_RECURSE SDK_SOURCES "${SDK}/src/*.cpp")
add_library(iot_sdk STATIC ${SDK_SOURCES})
target_compile_options(iot_sdk PRIVATE -w)
target_include_directories(iot_sdk PUBLIC "${SDK}/include")
set_target_properties(iot_sdk PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- 模块 ----
file(GLOB_RECURSE PLUGIN_SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp")
add_library(jsapi_napcat SHARED ${PLUGIN_SOURCES})
target_include_directories(jsapi_napcat PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_options(jsapi_napcat PRIVATE
  -Wall -Wextra -Werror=return-type -Wno-psabi -fvisibility=hidden)
target_link_libraries(jsapi_napcat PRIVATE
  iot_sdk pthread dl -Wl,-unresolved-symbols=ignore-all)

set_target_properties(jsapi_napcat PROPERTIES
  OUTPUT_NAME "jsapi_napcat"
  PREFIX "lib")
