cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(cdi_nes_controller C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(cdi_nes_controller
    main.c
)

# Genera header dal file PIO
pico_generate_pio_header(cdi_nes_controller
    ${CMAKE_CURRENT_LIST_DIR}/cdi_tx.pio
)

target_link_libraries(cdi_nes_controller
    pico_stdlib
    hardware_pio
    hardware_gpio
    hardware_clocks
)

# Abilita USB serial per debug
pico_enable_stdio_usb(cdi_nes_controller 1)
pico_enable_stdio_uart(cdi_nes_controller 0)

# Genera .uf2 e altri output
pico_add_extra_outputs(cdi_nes_controller)
