add_library(drregexrw SHARED regexrw.c)
target_link_libraries(drregexrw shamon-shmbuf shamon-list shamon-signature)
target_compile_options(drregexrw PUBLIC -Wno-pedantic -Wno-missing-field-initializers)

use_DynamoRIO_extension(drregexrw drmgr)
configure_DynamoRIO_client(drregexrw)

include_directories("${CMAKE_SOURCE_DIR}")
include_directories("${CMAKE_SOURCE_DIR}/shmbuf")
include_directories("${CMAKE_SOURCE_DIR}/streams")

