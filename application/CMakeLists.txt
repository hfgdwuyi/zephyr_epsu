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

# Firmware version - read from Kconfig.project so the build log can never
# disagree with the version compiled into the app (and signed into the image).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/Kconfig.project" _kcfg)
set(CIOS_ZHONG_FW_VERSION "")
set(_grab FALSE)
foreach(_line IN LISTS _kcfg)
	if(_line MATCHES "^config CIOS_ZHONG_FW_VERSION")
		set(_grab TRUE)
	elseif(_grab AND _line MATCHES "^[ \t]*default[ \t]+\"([^\"]+)\"")
		set(CIOS_ZHONG_FW_VERSION "${CMAKE_MATCH_1}")
		break()
	endif()
endforeach()
if(CIOS_ZHONG_FW_VERSION STREQUAL "")
	message(FATAL_ERROR "cannot read CIOS_ZHONG_FW_VERSION from Kconfig.project")
endif()
message(STATUS "Firmware version: ${CIOS_ZHONG_FW_VERSION}")

# Application modules
add_subdirectory(src/main)
add_subdirectory(src/state_machine)
add_subdirectory(src/sensor)
add_subdirectory(src/terminal)
add_subdirectory(src/ac_meter)
add_subdirectory(src/scheduler)
add_subdirectory(src/external)

# Board support package (hardware driver wrappers)
add_subdirectory(src/bsp ${CMAKE_CURRENT_BINARY_DIR}/bsp)

# Application modules
add_subdirectory(src/indicator)
