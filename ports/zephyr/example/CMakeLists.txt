# west build -b nrf52840dk/nrf52840 ports/zephyr/example -- -DZEPHYR_EXTRA_MODULES=$PWD
#   USB console instead of the UART: add -DEXTRA_CONF_FILE=overlay-usb.conf -DEXTRA_DTC_OVERLAY_FILE=usb.overlay
#   (boards/<board>.overlay is picked up automatically for ADC / PWM / I2S / CAN wiring)
# Options: Kconfig (prj.conf), and MCS_* options in a mcs_user_config.h next to this file or in
#   src/ (template: MicroCS/config/mcs_user_config.h), used automatically.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(microcs_zephyr)
target_sources(app PRIVATE src/main.c)
