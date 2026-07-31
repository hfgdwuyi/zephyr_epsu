/*
 * wdt_impl.c — Zephyr implementation of hal_wdt.h
 */

#include "../../application/hal/hal_wdt.h"
#include "bsp_wtdg.h"

void hal_wdt_init(void)
{
	WTDG_Init();
}

void hal_wdt_feed(void)
{
	WTDG_Feed();
}
