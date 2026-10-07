# MicroCS firmware for any ESP32-family board - a normal ESP-IDF project.
# MicroCS itself goes in components/MicroCS:
#   git clone https://github.com/amir1387aht/MicroCS components/MicroCS
#   idf.py set-target esp32s3     (esp32, esp32c3, esp32c6, esp32h2, esp32p4 ...)
#   idf.py build flash monitor    -> C# REPL on the serial port
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(microcs_esp32)
