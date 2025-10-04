cmake_minimum_required(VERSION 3.10)
project(WebServer)

# 基础设置
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

# 构建类型（默认 Release，可用 -DDEBUG=ON 启用 Debug）
option(DEBUG "Build with debug symbols" OFF)
if (DEBUG)
    set(CMAKE_BUILD_TYPE Debug)
    add_compile_options(-g -O0)
else()
    set(CMAKE_BUILD_TYPE Release)
    add_compile_options(-O2)
endif()

# 头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)

# 源文件（递归搜索 src 下的所有 cpp）
file(GLOB_RECURSE SRC_FILES
    ${PROJECT_SOURCE_DIR}/src/*.cpp
)

# 主程序
add_executable(web_server main.cpp ${SRC_FILES})
target_link_libraries(web_server pthread mysqlclient)

# 测试程序
file(GLOB TEST_SRC
    ${PROJECT_SOURCE_DIR}/test/*.cpp
)
if (TEST_SRC)
    add_executable(test ${TEST_SRC} ${SRC_FILES})
    target_link_libraries(test pthread mysqlclient)
endif()
