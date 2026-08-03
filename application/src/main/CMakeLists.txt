target_sources(app PRIVATE main.c)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../bsp
    ${CMAKE_CURRENT_LIST_DIR}/../state_machine
    ${CMAKE_CURRENT_LIST_DIR}/../scheduler
    ${CMAKE_CURRENT_LIST_DIR}/../objdic
    ${CMAKE_CURRENT_LIST_DIR}/../canopen/include
    ${CMAKE_CURRENT_LIST_DIR}/../candriver
)
