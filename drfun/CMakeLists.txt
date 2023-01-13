add_library(funs SHARED funs.c)
target_link_libraries(funs vamos-buffers-shmbuf vamos-buffers-list vamos-buffers-signature)
target_compile_options(funs PUBLIC -Wno-pedantic -Wno-missing-field-initializers)

use_DynamoRIO_extension(funs drmgr)
use_DynamoRIO_extension(funs drsyms)
use_DynamoRIO_extension(funs drreg)
use_DynamoRIO_extension(funs drutil)
configure_DynamoRIO_client(funs)

