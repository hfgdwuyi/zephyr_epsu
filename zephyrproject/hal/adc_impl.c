/*
 * adc_impl.c — Zephyr implementation of hal_adc.h
 */

#include "../../application/hal/hal_adc.h"
#include "bsp_ain.h"

void hal_adc_init(void)
{
	bspAinInit();
}

void hal_adc_poll(void)
{
	bspAinPoll();
}

uint32_t hal_adc_read_raw(hal_ain_channel_t ch)
{
	if (ch >= HAL_AIN_COUNT) return 0;
	return bspAinGetRawValue((uint8_t)ch);
}
