target_sources(app PRIVATE terminal.c)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../bsp
    ${CMAKE_CURRENT_LIST_DIR}/../sensor
    ${CMAKE_CURRENT_LIST_DIR}/../ac_meter
)
