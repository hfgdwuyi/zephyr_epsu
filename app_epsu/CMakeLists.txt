cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(CiosZhong_psu)

zephyr_linker_sources(SECTIONS sections-rom.ld)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../lib/common
    ${CMAKE_CURRENT_LIST_DIR}/../lib/bsp
    ${CMAKE_CURRENT_LIST_DIR}/../lib/http
)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../lib/common ${CMAKE_BINARY_DIR}/common)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../lib/bsp    ${CMAKE_BINARY_DIR}/bsp)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../lib/http   ${CMAKE_BINARY_DIR}/http)

target_sources(app PRIVATE
    main/main.c
)