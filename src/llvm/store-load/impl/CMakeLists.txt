add_library(store-load-impl
	STATIC impl.c)
target_include_directories(store-load-impl PRIVATE ${vamos_buffers_INCLUDE_DIRS})
