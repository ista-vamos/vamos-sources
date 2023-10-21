find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBEVDEV REQUIRED libevdev)
pkg_check_modules(LIBINPUT REQUIRED libinput)
pkg_check_modules(LIBUDEV REQUIRED libudev)

add_executable(vsrc-libinput libinput-debug-events.c shared.c util-strings.c)
target_compile_definitions(vsrc-libinput PRIVATE -D_POSIX_C_SOURCE=200809L)

target_link_libraries(vsrc-libinput PUBLIC ${LIBEVDEV_LIBRARIES})
target_include_directories(vsrc-libinput PRIVATE ${LIBEVDEV_INCLUDE_DIRS})
target_link_libraries(vsrc-libinput PUBLIC ${LIBUDEV_LIBRARIES})
target_include_directories(vsrc-libinput PRIVATE ${LIBUDEV_INCLUDE_DIRS})
target_link_libraries(vsrc-libinput PUBLIC ${LIBINPUT_LIBRARIES})
target_include_directories(vsrc-libinput PRIVATE ${LIBINPUT_INCLUDE_DIRS})
