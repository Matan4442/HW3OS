cmake_minimum_required(VERSION 3.19)
project(HW3OS C)

set(CMAKE_C_STANDARD 99)

add_executable(HW3OS threadpool.c segel.c server.c request.c )

target_link_libraries(${PROJECT_NAME} pthread)
