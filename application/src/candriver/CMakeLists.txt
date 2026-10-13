target_sources(app PRIVATE
    can_zephyr.c
    cdriver.c
    cpu_generic.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../objdic
    ${CMAKE_CURRENT_LIST_DIR}/../canopen/include
)
