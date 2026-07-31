target_sources(app PRIVATE
    board_impl.c
    gpio_impl.c
    adc_impl.c
    pwm_impl.c
    wdt_impl.c
    debug_impl.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../application/hal
    ${CMAKE_CURRENT_LIST_DIR}/../../lib/bsp
)
