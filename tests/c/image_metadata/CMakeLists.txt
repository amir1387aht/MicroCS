cmake_minimum_required(VERSION 3.13)
project(MicroCSImageMetadataTest C)
set(MICROCS_CONFIG NONE CACHE STRING "" FORCE)
set(MICROCS_MODULES OFF CACHE BOOL "" FORCE)
add_subdirectory(../../.. microcs-build)
add_executable(test_image_metadata ../test_image_metadata.c)
target_link_libraries(test_image_metadata PRIVATE MicroCS::microcs)
enable_testing()
add_test(NAME image_metadata COMMAND test_image_metadata)

# Host-only research probe, never part of the production library/runtime.
# Its self-test explicitly demonstrates a type-analysis gap; this is NOT a
# security-verifier gate or permission to execute handcrafted images.
add_executable(test_image_stack_probe ../experimental/test_image_stack_probe.c)
target_include_directories(test_image_stack_probe PRIVATE ../../../src)
target_link_libraries(test_image_stack_probe PRIVATE MicroCS::microcs)
add_test(NAME experimental_stack_flow COMMAND test_image_stack_probe --self-test)
