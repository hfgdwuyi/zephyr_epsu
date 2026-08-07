/*
 * terminal.c — change-triggered sensor value printing.
 *
 * terminalUpdate() is called periodically by the scheduler's terminal thread.
 * It reads the latest physical values from the sensor module and prints only
 * when a value has moved meaningfully (temperature > 3 °C, voltage > 3 V),
 * plus one initial snapshot at startup.
 */

/* C standard library */
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

/* Zephyr */
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

/* BSP */
#include "bsp_ain.h"

/* Application */
#include "terminal.h"
#include "sensor.h"

/* Voltage channels to monitor (PDC rails + monitor rails + mains). Temps are
 * handled separately via sensorTempGet1/2. */
static const uint8_t term_mv_chan[] = {
	AIN_ADC_PDC0, AIN_ADC_PDC1, AIN_ADC_PDC2, AIN_ADC_PDC3,
	AIN_ADC_PDC4, AIN_ADC_PDC5, AIN_ADC_PDC6, AIN_ADC_PDC7,
	AIN_ADC_PDC0_ALT,
	AIN_ADC_12V, AIN_ADC_5V0, AIN_ADC_3V3, AIN_ADC_VIN,
};

static uint32_t last_mv[ARRAY_SIZE(term_mv_chan)];  /* last printed rail values */
static int16_t  last_t1;
static int16_t  last_t2;
static bool     inited;

static void printTemp(const char *name, int16_t t)
{
	if (t == INT16_MIN) {
		printk("SENSOR %s: FAULT\n", name);
	} else if (t <= 0) {
		printk("SENSOR %s: n/a\n", name);
	} else {
		printk("SENSOR %s: %d.%d °C\n", name, t / 10, t % 10);
	}
}

void terminalUpdate(void)
{
	int16_t t1 = sensorTempGet1();
	int16_t t2 = sensorTempGet2();

	if (!inited) {
		/* initial snapshot once, then only print on change */
		printTemp("temp1", t1);
		printTemp("temp2", t2);
		for (size_t i = 0; i < ARRAY_SIZE(term_mv_chan); i++) {
			last_mv[i] = sensorGetPhys(term_mv_chan[i]);
			printk("SENSOR %s: %u.%03u V\n",
			       bspAinGetName(term_mv_chan[i]),
			       last_mv[i] / 1000U, last_mv[i] % 1000U);
		}
		last_t1 = t1;
		last_t2 = t2;
		inited = true;
		return;
	}

	/* temperature — print when it moved > 3 °C */
	int32_t dt1 = (int32_t)t1 - last_t1;
	int32_t dt2 = (int32_t)t2 - last_t2;
	if (dt1 < 0) dt1 = -dt1;
	if (dt2 < 0) dt2 = -dt2;
	if (dt1 >= TERMINAL_TEMP_DELTA && t1 > 0) {
		printTemp("temp1", t1);
		last_t1 = t1;
	}
	if (dt2 >= TERMINAL_TEMP_DELTA && t2 > 0) {
		printTemp("temp2", t2);
		last_t2 = t2;
	}

	/* voltage — print when it moved > 3 V */
	for (size_t i = 0; i < ARRAY_SIZE(term_mv_chan); i++) {
		uint32_t mv = sensorGetPhys(term_mv_chan[i]);
		uint32_t d = (mv > last_mv[i]) ? (mv - last_mv[i]) : (last_mv[i] - mv);
		if (d >= TERMINAL_MV_DELTA) {
			printk("SENSOR %s: %u.%03u V\n",
			       bspAinGetName(term_mv_chan[i]),
			       mv / 1000U, mv % 1000U);
			last_mv[i] = mv;
		}
	}
}
