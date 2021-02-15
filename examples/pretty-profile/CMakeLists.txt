cmake_minimum_required(VERSION 3.9)
project(pretty-profile VERSION 1 LANGUAGES C)

add_executable(pretty-profile ${PROJECT_SOURCE_DIR}/pretty-profile.c)
target_link_libraries(pretty-profile PRIVATE deadlock)
