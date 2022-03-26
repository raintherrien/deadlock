cmake_minimum_required(VERSION 3.13)
project(deadlock VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD          11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS        OFF)

set(DEADLOCK_SOURCES ${PROJECT_SOURCE_DIR}/src/dl.c
                     ${PROJECT_SOURCE_DIR}/src/graph.c
                     ${PROJECT_SOURCE_DIR}/src/sched.c
                     ${PROJECT_SOURCE_DIR}/src/tqueue.c
                     ${PROJECT_SOURCE_DIR}/src/worker.c)
add_library(deadlock ${DEADLOCK_SOURCES})

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

option(DEADLOCK_BUILD_BENCHMARKS "Build benchmarks" OFF)
mark_as_advanced(FORCE DEADLOCK_BUILD_BENCHMARKS)
if(DEADLOCK_BUILD_BENCHMARKS)
	add_subdirectory(bench/latency)

	find_program(CARGO_EXECUTABLE "cargo")
	if(CARGO_EXECUTABLE)
		add_custom_target(rs-async-std-latency-copy
		                  COMMAND ${CMAKE_COMMAND} -E copy_directory
		                  ${PROJECT_SOURCE_DIR}/bench/rs-async-std-latency
		                  ${CMAKE_CURRENT_BINARY_DIR}/bench/rs-async-std-latency)
		add_custom_target(rs-async-std-latency
		                  COMMAND ${CARGO_EXECUTABLE} build --release
		                  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bench/rs-async-std-latency)
		add_dependencies(rs-async-std-latency rs-async-std-latency-copy)
		add_dependencies(deadlock rs-async-std-latency)
	endif()
endif()

option(DEADLOCK_BUILD_EXAMPLES "Build examples" OFF)
mark_as_advanced(FORCE DEADLOCK_BUILD_EXAMPLES)
if(DEADLOCK_BUILD_EXAMPLES)
	add_subdirectory(examples/idle)
	add_subdirectory(examples/pretty-profile)
	add_subdirectory(examples/visualize-optick)
endif()

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

configure_file(${PROJECT_SOURCE_DIR}/include/deadlock/internal.h.in
               ${PROJECT_SOURCE_DIR}/include/deadlock/internal.h
               @ONLY)
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
