# MicroCS firmware for any ESP32-family board.
#   idf.py set-target esp32s3     (esp32, esp32c3, esp32c6, esp32h2, esp32p4 ...)
#   idf.py build flash monitor    -> C# REPL on the USB serial port
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../../..)   # the MicroCS repository = component "MicroCS"
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(microcs_esp32)
