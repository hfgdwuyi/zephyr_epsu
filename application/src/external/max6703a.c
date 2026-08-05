/*!
 * @file max6703a.c
 * @brief External watchdog MAX6703A (WDI) — application layer
 */
/*----------------------------------------------------------------------------*/

/* BSP */
#include "bsp_dio.h"

/* Application */
#include "max6703a.h"

void max6703aInit(void)
{
	/* WDI pin starts low; the first max6703aFeed() raises it. */
	bspDoutSetStatus(DOUT_WDI, false);
}

/* Toggle the external MAX6703A WDI bit in the DOUT bitmap; the 1ms
 * bspDoutSet() task applies it to the GPIO pin. Must be called within
 * the 1.6 s watchdog timeout. */
void max6703aFeed(void)
{
	bspDoutSetStatus(DOUT_WDI, !bspDoutGetStatus(DOUT_WDI));
}
