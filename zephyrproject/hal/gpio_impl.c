/*
 * gpio_impl.c — Zephyr implementation of hal_gpio.h
 *
 * Maps HAL logical channel names to physical GPIO pins via lib/bsp/bsp_dio.
 * All product-specific pin assignments live ONLY in the DOUT/DIN tables
 * inside lib/bsp/bsp_dio.c / lib/bsp/bsp_led.c — nowhere else.
 */

#include "../../application/hal/hal_gpio.h"

#include <zephyr/kernel.h>
#include "bsp_dio.h"
#include "bsp_led.h"

/* ---- Output: HAL logical channel → BSP DOUT index (1:1 for now) ---- */

void hal_gpio_out_set(hal_dout_channel_t ch, bool state)
{
	if (ch < HAL_CH_DOUT_COUNT) {
		bspDoutSet((uint8_t)ch, state);
	}
}

bool hal_gpio_out_read(hal_dout_channel_t ch)
{
	if (ch < HAL_CH_DOUT_COUNT) {
		return bspDoutRead((uint8_t)ch);
	}
	return false;
}

/* ---- Input: HAL logical channel → BSP DIN index (1:1 for now) ---- */

bool hal_gpio_in_get(hal_din_channel_t ch)
{
	if (ch < HAL_CH_DIN_COUNT) {
		return bspDinGet((uint8_t)ch);
	}
	return false;
}

/* ---- Output mirroring ---- */

void hal_gpio_out_mirror_inputs(void)
{
	bspDoutUpdate();
}

/* ---- Init ---- */

void hal_gpio_init(void)
{
	bspDioInit();
	ledInit();
}

/* ---- NUCLEO board LEDs ---- */

void hal_led_set(hal_led_t led, bool on)
{
	if (on) {
		ledSwitchOn((uint8_t)led);
	} else {
		ledSwitchOff((uint8_t)led);
	}
}

void hal_led_toggle(hal_led_t led)
{
	ledToggle((uint8_t)led);
}

void hal_led_init(void)
{
	ledInit();
}

/* ---- MAX6703A WDI ---- */

void hal_wdi_feed(void)
{
	bspWdiFeed();
}
