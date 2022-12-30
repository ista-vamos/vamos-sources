if (LLVM_SOURCES)
add_subdirectory(llvm)
endif()

if (DYNAMORIO_SOURCES)
       add_subdirectory(drfun)
       add_subdirectory(drregex)
       add_subdirectory(drregexrw)
       add_subdirectory(drsyscalls)
endif()

if (BPF_SOURCES)
	add_subdirectory(bcc)
endif()

add_executable(sendaddr sendaddr.c)
add_executable(regex regex.c)
add_executable(regexd regex-drrio.c)
add_executable(regexdrw regex-drrio-rw.c)

target_compile_definitions(regex PRIVATE -D_POSIX_C_SOURCE=200809L)
target_compile_definitions(regexd PRIVATE -D_POSIX_C_SOURCE=200809L)
target_compile_definitions(regexdrw PRIVATE -D_POSIX_C_SOURCE=200809L)

target_include_directories(sendaddr PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(regex PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(regexd PRIVATE ${CMAKE_SOURCE_DIR})

target_include_directories(regex PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(regexd PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(regexdrw PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(regex PRIVATE shamon-shmbuf shamon-signature)
target_link_libraries(regexd PRIVATE shamon-shmbuf shamon-signature shamon-fastbuf pthread)
target_link_libraries(regexdrw PRIVATE shamon-shmbuf shamon-signature shamon-fastbuf pthread)
target_link_libraries(sendaddr PRIVATE shamon-shmbuf shamon-signature)

if (IPO)
        set_property(TARGET sendaddr PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        set_property(TARGET regex PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        set_property(TARGET regexd PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        set_property(TARGET regexdrw PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
