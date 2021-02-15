cmake_minimum_required(VERSION 3.9)
project(visualize-optick VERSION 1 LANGUAGES C)

find_package(Optick QUIET)

if(Optick_FOUND)
	add_executable(visualize-optick ${PROJECT_SOURCE_DIR}/visualize-optick.c)
	if(UNIX)
		# Required POSIX version for nanosleep, clock_gettime
		target_compile_definitions(visualize-optick PRIVATE _POSIX_C_SOURCE=199309L)
	endif()
	target_link_libraries(visualize-optick PRIVATE deadlock OptickCore)
else()
	message(STATUS "Skipping examples/visualize-optick because it requires Optick.")
endif()
