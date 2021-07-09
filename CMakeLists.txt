cmake_minimum_required(VERSION 3.13)
project(deadlock VERSION 0.1 LANGUAGES C)

set(CMAKE_C_STANDARD          11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS        OFF)

include(GNUInstallDirs)

# Try to compile release
set(DEFAULT_BUILD_TYPE "Release")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	message(STATUS "Setting build type to '${DEFAULT_BUILD_TYPE}' as none was specified.")
	set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}" CACHE STRING "Choose the type of build." FORCE)
	# Set the possible values of build type for cmake-gui
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Add warnings
# Note: add_compile_options propegates these options to e.g. examples.
set(DEADLOCK_MSVC_WARNFLAGS /W4 /WX)
set(DEADLOCK_OTHR_WARNFLAGS -Wall -Werror -Wextra)
add_compile_options("$<IF:$<C_COMPILER_ID:MSVC>,${DEADLOCK_MSVC_WARNFLAGS},${DEADLOCK_OTHR_WARNFLAGS}>")

set(DEADLOCK_SOURCES ${PROJECT_SOURCE_DIR}/src/dl.c
                     ${PROJECT_SOURCE_DIR}/src/graph.c
                     ${PROJECT_SOURCE_DIR}/src/sched.c
                     ${PROJECT_SOURCE_DIR}/src/tqueue.c
                     ${PROJECT_SOURCE_DIR}/src/worker.c)
add_library(deadlock ${DEADLOCK_SOURCES})
target_include_directories(deadlock PUBLIC
                           $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                           $<INSTALL_INTERFACE:include>)
target_link_libraries(deadlock PUBLIC Threads::Threads)

# Disable warnings about fopen in VS2019
if(WIN32)
	target_compile_definitions(deadlock PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Required POSIX version for nanosleep in src/sched.c
if(UNIX)
	target_compile_definitions(deadlock PRIVATE _POSIX_C_SOURCE=199309L)
endif()

install(TARGETS deadlock
        EXPORT deadlock
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/deadlock
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT deadlock
        FILE deadlock-config.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/deadlock)

option(DEADLOCK_BUILD_SAN "Build with sanitizers" OFF)
if(DEADLOCK_BUILD_SAN)
	target_compile_options(deadlock PUBLIC -fsanitize=address,leak,undefined,pointer-compare,pointer-subtract -fstack-protector)
	target_link_options   (deadlock PUBLIC -fsanitize=address,leak,undefined,pointer-compare,pointer-subtract -fstack-protector)
endif()

option(DEADLOCK_BUILD_TUNE "Build for this architecture" OFF)
if(DEADLOCK_BUILD_TUNE)
	target_compile_options(deadlock PUBLIC -march=native)
endif()

option(DEADLOCK_GRAPH_EXPORT "Build with graph export support" ON)
if(DEADLOCK_GRAPH_EXPORT)
	target_compile_definitions(deadlock PUBLIC DEADLOCK_GRAPH_EXPORT=1)
else()
	target_compile_definitions(deadlock PUBLIC DEADLOCK_GRAPH_EXPORT=0)
endif()

option(DEADLOCK_BUILD_EXAMPLES "Build example programs" OFF)
mark_as_advanced(FORCE DEADLOCK_BUILD_EXAMPLES)
if(DEADLOCK_BUILD_EXAMPLES)
	add_subdirectory(examples/http-server)
	add_subdirectory(examples/idle)
	add_subdirectory(examples/latency)
	add_subdirectory(examples/pretty-profile)
	add_subdirectory(examples/visualize-optick)
endif()

