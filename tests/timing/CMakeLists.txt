cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(timing_tests)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../lib/common
)

target_sources(app PRIVATE
    src/test_timing.c
    ../../lib/common/timing.c
)