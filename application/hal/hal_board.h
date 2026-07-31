/*
 * hal_board.h — 板级初始化抽象接口 (RTOS无关)
 */

#ifndef HAL_BOARD_H
#define HAL_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化所有外设：GPIO, ADC, DAC, PWM, 看门狗 */
void hal_board_init(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_BOARD_H */
