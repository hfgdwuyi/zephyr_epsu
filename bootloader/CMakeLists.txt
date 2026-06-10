cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(epsu_bootloader)

# Read bootloader version from local FW_VERSION file.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/FW_VERSION")
file(READ "${CMAKE_CURRENT_LIST_DIR}/FW_VERSION" FW_VERSION)
string(STRIP "${FW_VERSION}" FW_VERSION)
if(NOT FW_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "Invalid VERSION format: '${FW_VERSION}'. Expected MAJOR.MINOR.PATCH (e.g. 0.1.0)")
endif()

# Guard against shipping bootloader and application with identical versions.
set(APP_VERSION_FILE "${CMAKE_CURRENT_LIST_DIR}/../application/FW_VERSION")
set(APP_VERSION "unknown")
if(EXISTS "${APP_VERSION_FILE}")
    file(READ "${APP_VERSION_FILE}" APP_VERSION)
    string(STRIP "${APP_VERSION}" APP_VERSION)
    if(FW_VERSION STREQUAL APP_VERSION)
        message(WARNING "Bootloader VERSION (${FW_VERSION}) equals application VERSION (${APP_VERSION}); consider setting distinct versions.")
    endif()
endif()

message(STATUS "Bootloader version: ${FW_VERSION}")
message(STATUS "Application version: ${APP_VERSION}")
target_compile_definitions(app PRIVATE
    BUILD_VERSION="${FW_VERSION}"
    BOOT_VERSION="${FW_VERSION}"
    APP_VERSION="${APP_VERSION}"
    BOOTLOADER=1
)

# Custom linker section for HTTP resource registration
zephyr_linker_sources(SECTIONS sections-rom.ld)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../lib/common
    ${CMAKE_CURRENT_LIST_DIR}/../lib/common/dm
    ${CMAKE_CURRENT_LIST_DIR}/../lib/bsp
    ${CMAKE_CURRENT_LIST_DIR}/../lib/http
)

target_sources(app PRIVATE
    src/main.c
    ../lib/common/dm/dm_core.c
    ../lib/common/dm/dm_ctrl_worker.c
    ../lib/common/dm/dm_diag_worker.c
    ../lib/common/dm/dm_update_worker.c
    ../lib/common/message_queue.c
    ../lib/bsp/bsp_led.c
    ../lib/bsp/bsp_board.c
    ../lib/bsp/bsp_ain.c
    ../lib/bsp/bsp_aout.c
    ../lib/bsp/bsp_dio.c
    ../lib/bsp/bsp_pwm.c
    ../lib/bsp/bsp_wtdg.c
    ../lib/http/http_api.c
    ../lib/http/http_routes.c
    ../lib/http/http_task_echo.c
    ../lib/http/http_task_uptime.c
    ../lib/http/http_task_heartbeat.c
    ../lib/http/http_task_update.c
    ../lib/http/http_task_control.c
    ../lib/http/http_task_diag.c
    ../lib/http/http_task_diag_debug.c
    ../lib/http/http_task_version.c
    ../lib/http/http_task_bootloader.c
)
