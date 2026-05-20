target_sources(app PRIVATE
    dm_core.c
    dm_update_worker.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)