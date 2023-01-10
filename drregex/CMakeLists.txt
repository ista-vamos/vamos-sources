# for some reason, DRIO has problems with loading libraries if linked...
set(CORE_DIR ../../core)
set(SHMBUF_DIR ../../shmbuf)
add_library(drregex SHARED regex.c
                           ${SHMBUF_DIR}/buffer.c
                           ${SHMBUF_DIR}/buffer-aux.c ${SHMBUF_DIR}/buffer-sub.c
                           ${SHMBUF_DIR}/shm.c ${SHMBUF_DIR}/client.c
                           ${CORE_DIR}/source.c ${CORE_DIR}/par_queue.c
                           ${CORE_DIR}/spsc_ringbuf.c ${CORE_DIR}/list.c
                           ${CORE_DIR}/utils.c ${CORE_DIR}/signatures.c ${CORE_DIR}/event.c)
target_compile_options(drregex PUBLIC -Wno-pedantic -Wno-missing-field-initializers)
target_compile_definitions(drregex PUBLIC -D_POSIX_C_SOURCE=200809L)

use_DynamoRIO_extension(drregex drmgr)
configure_DynamoRIO_client(drregex)

add_library(drregex-mt SHARED regex-mt.c
                           ${SHMBUF_DIR}/buffer.c
                           ${SHMBUF_DIR}/buffer-aux.c ${SHMBUF_DIR}/buffer-sub.c
                           ${SHMBUF_DIR}/shm.c ${SHMBUF_DIR}/client.c
                           ${CORE_DIR}/source.c ${CORE_DIR}/par_queue.c
                           ${CORE_DIR}/spsc_ringbuf.c ${CORE_DIR}/list.c ${CORE_DIR}/list-embedded.c
                           ${CORE_DIR}/utils.c ${CORE_DIR}/signatures.c ${CORE_DIR}/event.c)
target_compile_options(drregex-mt PUBLIC -Wno-pedantic -Wno-missing-field-initializers -g)
target_compile_definitions(drregex-mt PUBLIC -D_POSIX_C_SOURCE=200809L)

use_DynamoRIO_extension(drregex-mt drmgr)
configure_DynamoRIO_client(drregex-mt)


include_directories("${CMAKE_SOURCE_DIR}")
include_directories("${CMAKE_SOURCE_DIR}/shmbuf")
include_directories("${CMAKE_SOURCE_DIR}/streams")

