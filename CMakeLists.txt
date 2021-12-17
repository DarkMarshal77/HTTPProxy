cmake_minimum_required(VERSION 3.15)
project(HTTP_Proxy_Server)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

add_executable(HTTP_Proxy_Server httpserver.cpp libhttp.cpp wq.cpp management.cpp management.h)