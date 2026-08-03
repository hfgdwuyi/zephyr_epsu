cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(CiosZhong_psu)

# Firmware version — hardcoded, single source of truth
message(STATUS "Firmware version: 0.1.0")

# Application modules
add_subdirectory(src/main)
add_subdirectory(src/state_machine)
add_subdirectory(src/ntc)
add_subdirectory(src/scheduler)

# Board support package (hardware driver wrappers)
add_subdirectory(src/bsp ${CMAKE_CURRENT_BINARY_DIR}/bsp)
