cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(CiosZhong_psu)

# Read firmware version from VERSION file
file(READ "${CMAKE_CURRENT_LIST_DIR}/../VERSION" FW_VERSION)
string(STRIP "${FW_VERSION}" FW_VERSION)
if(NOT FW_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "Invalid VERSION format: '${FW_VERSION}'. Expected MAJOR.MINOR.PATCH (e.g. 0.1.0)")
endif()
message(STATUS "Firmware version: ${FW_VERSION}")
target_compile_definitions(app PRIVATE BUILD_VERSION="${FW_VERSION}")

# HAL headers visible to all application code
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/hal
    ${CMAKE_CURRENT_LIST_DIR}/src/state_machine
    ${CMAKE_CURRENT_LIST_DIR}/src/ntc
)

# Application entry point
target_sources(app PRIVATE src/main.c)

# Application modules (RTOS-independent)
add_subdirectory(src/state_machine)
add_subdirectory(src/ntc)

# Platform HAL implementations (Zephyr-specific)
add_subdirectory(../zephyrproject/hal ${CMAKE_CURRENT_BINARY_DIR}/hal)

# Board support package (Zephyr hardware drivers)
add_subdirectory(../lib/bsp ${CMAKE_CURRENT_BINARY_DIR}/bsp)
