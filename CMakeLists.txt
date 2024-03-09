cmake_minimum_required(VERSION 3.13)
include("C:/SRC/pico-sdk/pico_sdk_init.cmake")
pico_sdk_init()

project(book_kbd)

add_executable(book_kbd
        book_kbd.c
        )

pico_enable_stdio_usb(book_kbd 0)
pico_enable_stdio_uart(book_kbd 1)

target_include_directories(book_kbd PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(book_kbd pico_stdlib tinyusb_host tinyusb_board hardware_pio)

pico_add_extra_outputs(book_kbd)

