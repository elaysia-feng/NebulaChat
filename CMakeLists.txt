cmake_minimum_required(VERSION 3.10)
project(NebulaChat)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wall")

# 关键：告诉编译器从 include/ 下开始找头文件
include_directories(${CMAKE_SOURCE_DIR}/include)

# 把所有源文件都编译进去
add_executable(NebulaChat
    src/main.cpp
    src/core/ThreadPool.cpp
)
