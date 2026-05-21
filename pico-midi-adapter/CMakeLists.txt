set(PICO_BOARD waveshare_rp2350_zero CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)

project(pico_midi_adapter C CXX ASM)

pico_sdk_init()

add_executable(pico_midi_adapter
    src/main.c
    src/usb_descriptors.c
)

target_include_directories(pico_midi_adapter PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_compile_definitions(pico_midi_adapter PRIVATE
    PICO_TINYUSB_DEVICE=1
)

target_link_libraries(pico_midi_adapter PRIVATE
    pico_stdlib
    tinyusb_device
    tinyusb_board
    hardware_uart
    hardware_gpio
    hardware_pio
)

pico_enable_stdio_usb(pico_midi_adapter 0)
pico_enable_stdio_uart(pico_midi_adapter 0)

pico_add_extra_outputs(pico_midi_adapter)
