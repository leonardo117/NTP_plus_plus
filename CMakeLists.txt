cmake_minimum_required(VERSION 3.10)
project(ntp_client)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加ASIO库
include_directories("${CMAKE_SOURCE_DIR}")


# 添加可执行文件
add_executable(ntp_client 
    sntp/sntp.cpp
    sntp/main.cpp
)

# 链接系统库
if(WIN32)
    target_link_libraries(ntp_client ws2_32)  # Windows需要链接 Winsock 库
else()
    target_link_libraries(ntp_client pthread) # Linux需要链接 pthread 库
endif()