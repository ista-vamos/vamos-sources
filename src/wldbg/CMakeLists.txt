if (NOT wldbg_DIR)
	message(FATAL_ERROR "Need to know where wldbg is. Set wldbg_DIR to the root directory of wldbg" )
endif()

add_library(wldbg-pass SHARED pass.c)
target_include_directories(wldbg-pass PRIVATE ${wldbg_DIR}/src)
