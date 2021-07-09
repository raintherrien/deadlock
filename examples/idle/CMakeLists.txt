cmake_minimum_required(VERSION 3.9)
project(idle VERSION 1 LANGUAGES C)

add_executable(idle ${PROJECT_SOURCE_DIR}/idle.c)
target_link_libraries(idle PRIVATE deadlock)
