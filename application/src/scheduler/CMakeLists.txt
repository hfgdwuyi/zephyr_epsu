target_sources(app PRIVATE scheduler.c)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../bsp
    ${CMAKE_CURRENT_LIST_DIR}/../state_machine
)
