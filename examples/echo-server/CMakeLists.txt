cmake_minimum_required(VERSION 3.9)
project(echo-server VERSION 1 LANGUAGES C)

if(UNIX)
	add_executable(echo-server ${PROJECT_SOURCE_DIR}/echo-server.c)
	# Required POSIX version for networking components
	target_compile_definitions(echo-server PRIVATE _POSIX_C_SOURCE=200112L)
	target_link_libraries(echo-server PRIVATE deadlock)
else()
	message(STATUS "Skipping examples/echo-server because it requires POSIX.")
endif()
