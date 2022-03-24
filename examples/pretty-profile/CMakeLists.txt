cmake_minimum_required(VERSION 3.9)
project(pretty-profile VERSION 1 LANGUAGES C)

add_executable(pretty-profile ${PROJECT_SOURCE_DIR}/pretty-profile.c)
# Required POSIX version for nanosleep
if(UNIX)
        target_compile_definitions(pretty-profile PRIVATE _POSIX_C_SOURCE=199309L)
endif()
target_link_libraries(pretty-profile PRIVATE deadlock)
