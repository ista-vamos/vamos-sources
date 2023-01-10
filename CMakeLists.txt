cmake_minimum_required(VERSION 3.9)
project(vamos-sources LANGUAGES C VERSION 0.1)

set(CMAKE_VERBOSE_MAKEFILE 1)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(shamon REQUIRED)

include_directories(${shamon_INCLUDE_DIRS})

OPTION(DYNAMORIO_SOURCES "Build sources based on DynamoRIO" ON)
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
        find_package(DynamoRIO)
        if (NOT DynamoRIO_FOUND)
	        message(FATAL_ERROR
                        "DynamoRIO package required to build DynamoRIO sources")
        endif(NOT DynamoRIO_FOUND)
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

if (IPO)
        set_property(TARGET sendaddr PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        set_property(TARGET regex PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        set_property(TARGET regexd PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        set_property(TARGET regexdrw PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
