# west build -b nrf52840dk/nrf52840 ports/zephyr/example -- -DZEPHYR_EXTRA_MODULES=$PWD
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(microcs_zephyr)
target_sources(app PRIVATE src/main.c)
