cmake_minimum_required(VERSION 3.1)

add_llvm_library(store-load-instrumentation
             MODULE StoreLoadInstrumentation.cpp
         PLUGIN_TOOL opt)
target_include_directories(store-load-instrumentation SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})

add_subdirectory(impl)
