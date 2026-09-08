target_sources(app PRIVATE terminal.c uart_cmd.c)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../bsp
    ${CMAKE_CURRENT_LIST_DIR}/../sensor
    ${CMAKE_CURRENT_LIST_DIR}/../ac_meter
    ${CMAKE_CURRENT_LIST_DIR}/../state_machine
)
