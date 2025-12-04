cmake_minimum_required(VERSION 3.10)
project(NebulaChat)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0")


# include
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    /usr/include/mysql
    /usr/include/hiredis        # <--- 新增 hiredis 头文件
)

# Thread
find_package(Threads REQUIRED)

# MySQL
find_library(MYSQLCLIENT_LIB
    NAMES mysqlclient
    PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib
)
if(NOT MYSQLCLIENT_LIB)
    message(FATAL_ERROR "libmysqlclient not found (install libmysqlclient-dev)")
endif()
message(STATUS "Found MySQL: ${MYSQLCLIENT_LIB}")

# hiredis（手动查找）
find_library(HIREDIS_LIB hiredis
    PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib
)
if(NOT HIREDIS_LIB)
    message(FATAL_ERROR "hiredis not found (sudo apt install libhiredis-dev)")
endif()
message(STATUS "Found hiredis: ${HIREDIS_LIB}")

find_package(OpenSSL REQUIRED)
message(STATUS "Found OpenSSL: ${OPENSSL_VERSION}")

# sources
add_executable(NebulaChat
    src/main.cpp

    src/core/ThreadPool.cpp
    src/core/Reactor.cpp
    src/core/Server.cpp

    src/chat/AuthService.cpp
    src/chat/MessageHandler.cpp
    src/chat/SmsService.cpp
    src/chat/ChatHistory.cpp

    src/db/DBconnection.cpp
    src/db/DBpool.cpp
    src/db/RedisConnection.cpp
    src/db/RedisPool.cpp

    src/infra/redis/redis_client_impl.cpp
)

# link
target_link_libraries(NebulaChat
    PRIVATE
        Threads::Threads
        ${HIREDIS_LIB}        # <--- 链接 hiredis
        ${MYSQLCLIENT_LIB}    # <--- 链接 mysqlclient
        OpenSSL::Crypto
)
