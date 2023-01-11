cmake_minimum_required(VERSION 3.9)
project(vamos-sources LANGUAGES C VERSION 0.1)

set(CMAKE_VERBOSE_MAKEFILE 1)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(shamon REQUIRED)

if (shamon_ENABLE_IPO)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)  
endif()

include_directories(${shamon_INCLUDE_DIRS})

OPTION(DYNAMORIO_SOURCES "Build sources based on DynamoRIO" ON)
OPTION(BUILD_DYNAMORIO "Build DynamoRIO from sources" OFF)
OPTION(BUILD_DYNAMORIO_IF_MISSING "Build DynamoRIO from sources if it is not present" OFF)

OPTION(BPF_SOURCES "Build and use eBPF sources" OFF)
OPTION(LLVM_SOURCES "Build and use LLVM sources" ON)

if (BPF_SOURCES)
	ExternalProject_Add(bcc
			    GIT_REPOSITORY https://github.com/mchalupa/bcc
			    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bcc
                            GIT_TAG readwrite
			    INSTALL_COMMAND ""
			    BUILD_IN_SOURCE 1)

	add_compile_definitions(BPF_SOURCES)
endif()

if (DYNAMORIO_SOURCES)
		get_filename_component(DRIO_ABSPATH "ext/dynamorio/build/cmake" ABSOLUTE)
		find_package(DynamoRIO QUIET PATHS DRIO_ABSPATH)
       		if (NOT DynamoRIO_FOUND)
       		        message(FATAL_ERROR
				"DynamoRIO package required to build DynamoRIO sources. "
				"If you have DynamoRIO built, use DynamoRIO_DIR=<path> to point to the cmake config. "
				"If you want to build DynamoRIO, you can use the following command:\n"
				"make -C ext dynamorio CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}\n"
				"If cmake cannot find dynamorio then, try configuring  with -DDynamoRIO_DIR=ext/dynamorio/build/cmake".
				" You can disable building DynamoRIO sources with -DDYNAMORIO_SOURCES=OFF")
       		endif()

endif()


if (LLVM_SOURCES)
	add_subdirectory(llvm)
endif()

if (DYNAMORIO_SOURCES)
       add_subdirectory(drfun)
       add_subdirectory(drregex)
       add_subdirectory(drsyscalls)
endif()

if (BPF_SOURCES)
	add_subdirectory(bcc)
endif()

add_executable(sendaddr sendaddr.c)
add_executable(regex regex.c)

target_compile_definitions(regex PRIVATE -D_POSIX_C_SOURCE=200809L)

target_include_directories(sendaddr PRIVATE ${CMAKE_SOURCE_DIR})

target_include_directories(regex PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(regex    PRIVATE shamon-client)
target_link_libraries(sendaddr PRIVATE shamon-client)

