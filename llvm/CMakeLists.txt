cmake_minimum_required(VERSION 3.1)

# FIXME: Unfortunately, C is (at least temporarily) required due to a bug
# in LLVM 14.  See https://github.com/llvm/llvm-project/issues/53950.
project(race-instrumentation LANGUAGES C CXX)

include(CTest)

# we need at least C++11 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

cmake_policy(SET CMP0057 NEW)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "LLVM binaries: ${LLVM_TOOLS_BINARY_DIR}")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${LLVM_DIR}")
include(LLVMConfig)
include(AddLLVM)

message(STATUS "LLVM include dir: ${LLVM_INCLUDE_DIRS}")
message(STATUS "LLVM libraries dir: ${LLVM_LIBRARY_DIRS}")
message(STATUS "LLVM definitions: ${LLVM_DEFINITIONS}")
message(STATUS "Using compiler: ${CMAKE_CXX_COMPILER}")



add_llvm_library(race-instrumentation
		 MODULE RaceInstrumentation.cpp
		 PLUGIN_TOOL opt)
target_include_directories(race-instrumentation SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
