target_sources(app PRIVATE
    timing.c
    message_queue.c
    crc_calc.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

add_subdirectory(dm)