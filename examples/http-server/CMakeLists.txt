cmake_minimum_required(VERSION 3.9)
project(http-server VERSION 1 LANGUAGES C)

if(UNIX)
	add_executable(http-server ${PROJECT_SOURCE_DIR}/http-server.c)
	# Required POSIX version for networking components
	target_compile_definitions(http-server PRIVATE _POSIX_C_SOURCE=200112L)
	target_link_libraries(http-server PRIVATE deadlock)
endif()
