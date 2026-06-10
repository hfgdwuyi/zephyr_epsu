cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(message_queue_tests)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../lib/common
)

target_sources(app PRIVATE
    src/test_message_queue.c
    ../../lib/common/message_queue.c
)