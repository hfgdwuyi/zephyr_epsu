/*
 * board_impl.c — Zephyr implementation of hal_board.h
 */

#include "../../application/hal/hal_board.h"
#include "../../application/hal/hal_gpio.h"
#include "../../application/hal/hal_adc.h"
#include "../../application/hal/hal_pwm.h"
#include "bsp_board.h"

void hal_board_init(void)
{
	boardInit();       /* UART, I2C, SPI peripheral checks + ledInit, AinInit, AoutInit, PwmInit */

	/* HAL layer owns DIN polling startup */
}
