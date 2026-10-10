# CI check of the CMake integration: mcs_user_config.h next to this file is
# found automatically (MICROCS_CONFIG empty), MICROCS_PROFILE becomes the
# default profile that the header overrides, -D options still win.
#   cmake -S tests/c/config -B build/cfg && cmake --build build/cfg && build/cfg/test_config
cmake_minimum_required(VERSION 3.13)
project(microcs_config_test C)
set(MICROCS_PROFILE min)
add_subdirectory(../../.. microcs)
target_compile_definitions(microcs PUBLIC MCS_GC_GROW=3)
add_executable(test_config ../test_config.c)
target_link_libraries(test_config PRIVATE microcs)
