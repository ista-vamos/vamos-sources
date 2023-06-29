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

target_link_libraries(regex    PRIVATE vamos-buffers-client)
target_link_libraries(sendaddr PRIVATE vamos-buffers-client)

