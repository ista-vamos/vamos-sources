list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

# Get target arch
execute_process(COMMAND uname -m
  COMMAND sed "s/x86_64/x86/"
  OUTPUT_VARIABLE ARCH_output
  ERROR_VARIABLE ARCH_error
  RESULT_VARIABLE ARCH_result
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(${ARCH_result} EQUAL 0)
  set(ARCH ${ARCH_output})
  message(STATUS "BPF target arch: ${ARCH}")
else()
  message(FATAL_ERROR "Failed to determine target architecture: ${ARCH_error}")
endif()

# Set BpfObject input parameters -- note this is usually not necessary unless
# you're in a highly vendored environment (like libbpf-bootstrap)
set(LIBBPF_TOOLS_DIR ${PROJECT_SOURCE_DIR}/bcc-src/libbpf-tools)
set(LIBBPF_OUTPUT_DIR ${LIBBPF_TOOLS_DIR}/.output)
set(BPFOBJECT_VMLINUX_H ${LIBBPF_TOOLS_DIR}/${ARCH}/vmlinux.h)
set(LIBBPF_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/bcc-src/cc/libbpf/src")
set(LIBBPF_LIBRARIES ${LIBBPF_OUTPUT_DIR}/libbpf/libbpf.a)
find_package(BpfObject REQUIRED)

set(LIBBPF_HELPER_OBJS ${LIBBPF_OUTPUT_DIR}/btf_helpers.o
                       ${LIBBPF_OUTPUT_DIR}/uprobe_helpers.o
                       ${LIBBPF_OUTPUT_DIR}/map_helpers.o
                       ${LIBBPF_OUTPUT_DIR}/errno_helpers.o
                       ${LIBBPF_OUTPUT_DIR}/syscall_helpers.o
                       ${LIBBPF_OUTPUT_DIR}/trace_helpers.o)

# Create an executable for each application
file(GLOB apps *.bpf.c)
foreach(app ${apps})
  get_filename_component(app_stem ${app} NAME_WE)

  # Build object skeleton and depend skeleton on libbpf build
  bpf_object(${app_stem} ${app_stem}.bpf.c)
  add_dependencies(${app_stem}_skel libbpf-build)

  add_executable(${app_stem} ${app_stem}.c)
  target_link_libraries(${app_stem} ${app_stem}_skel ${LIBBPF_HELPER_OBJS})
  target_compile_options(${app_stem} PRIVATE -Wno-error)
  target_include_directories(${app_stem}
                             PRIVATE ${PROJECT_SOURCE_DIR}/bcc-src/libbpf-tools)
endforeach()
