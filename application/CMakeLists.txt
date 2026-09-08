cmake_minimum_required(VERSION 3.20.0)

# Custom board target (cioszhong_psu) lives in <app>/boards — register the
# application directory as a board root so
#   west build -b cioszhong_psu/stm32h745xx/m7
# finds it without passing -DBOARD_ROOT manually. A user-provided
# -DBOARD_ROOT on the command line still takes precedence.
if(NOT DEFINED BOARD_ROOT)
	set(BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR} CACHE STRING "Board root for custom boards")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(CiosZhong_psu)

# Firmware version — hardcoded, single source of truth
message(STATUS "Firmware version: 0.1.0")

# Application modules
add_subdirectory(src/main)
add_subdirectory(src/state_machine)
add_subdirectory(src/sensor)
add_subdirectory(src/terminal)
add_subdirectory(src/ac_meter)
add_subdirectory(src/scheduler)
add_subdirectory(src/external)
add_subdirectory(src/candriver)
add_subdirectory(src/objdic)
add_subdirectory(src/canopen)

# Board support package (hardware driver wrappers)
add_subdirectory(src/bsp ${CMAKE_CURRENT_BINARY_DIR}/bsp)
