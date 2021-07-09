cmake_minimum_required(VERSION 3.9)
project(latency VERSION 1 LANGUAGES C)

add_executable(latency ${PROJECT_SOURCE_DIR}/latency.c)
target_link_libraries(latency PRIVATE deadlock)
