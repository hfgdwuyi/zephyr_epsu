target_sources(app PRIVATE
    bsp_ain.c
    bsp_aout.c
    bsp_board.c
    bsp_dio.c
    bsp_led.c
    bsp_pwm.c
    bsp_wtdg.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
