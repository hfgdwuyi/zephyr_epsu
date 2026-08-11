target_sources(app PRIVATE max6703a.c)
target_sources(app PRIVATE at24c04.c)
target_sources(app PRIVATE is25lp064.c)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../bsp
)
