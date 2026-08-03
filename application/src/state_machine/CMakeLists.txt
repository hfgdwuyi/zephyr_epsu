target_sources(app PRIVATE psu_sm.c)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../bsp
    ${CMAKE_CURRENT_LIST_DIR}/../ntc
)
