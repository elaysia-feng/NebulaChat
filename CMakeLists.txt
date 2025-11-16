cmake_minimum_required(VERSION 3.10)
project(NebulaChat)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 线程库
find_package(Threads REQUIRED)

# MySQL 头文件路径
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    /usr/include/mysql
)

# 查找 MySQL 客户端库：libmysqlclient.so
find_library(MYSQLCLIENT_LIB
    NAMES mysqlclient
    PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib
)

if(NOT MYSQLCLIENT_LIB)
    message(FATAL_ERROR "Could not find libmysqlclient (install libmysqlclient-dev ?)")
endif()

message(STATUS "Found MySQL client library: ${MYSQLCLIENT_LIB}")

# 源文件
add_executable(NebulaChat
    src/main.cpp
    src/core/ThreadPool.cpp
    src/core/Reactor.cpp
    src/core/Server.cpp
    src/chat/MessageHandler.cpp
    src/chat/AuthService.cpp
    src/db/DBconnection.cpp
    src/db/DBpool.cpp
)
target_include_directories(NebulaChat PRIVATE include)

# 链接 pthread + mysqlclient
target_link_libraries(NebulaChat
    PRIVATE
        Threads::Threads
        ${MYSQLCLIENT_LIB}
)
