add_library(syscalls SHARED
            syscalls.c
            ../../fastbuf/shm_monitored.c
            ../../fastbuf/shm_common.c)
target_compile_options(syscalls PUBLIC -Wno-pedantic -Wno-missing-field-initializers)
include_directories("${CMAKE_SOURCE_DIR}/")

use_DynamoRIO_extension(syscalls drmgr)
configure_DynamoRIO_client(syscalls)

