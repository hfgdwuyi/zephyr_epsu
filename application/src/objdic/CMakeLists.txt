target_sources(app PRIVATE
    co_init.c
    objects.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../canopen/include
    ${CMAKE_CURRENT_LIST_DIR}/../candriver
)
