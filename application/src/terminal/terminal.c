/*
 * terminal.c — change-triggered sensor value printing.
 *
 * terminalUpdate() is called periodically by the scheduler's terminal thread.
 * It reads the latest physical values from the sensor module and prints only
 * when a value has moved meaningfully (temperature > 3 °C, voltage > 3 V),
 * plus one initial snapshot at startup.
 */

/* Standard library */
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

/* Zephyr */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

/* BSP */
#include "bsp_ain.h"

/* Application */
#include "terminal.h"
#include "uart_cmd.h"
#include "sensor.h"
#include "ac_meter.h"

/* Voltage channels to monitor (PDC rails + monitor rails). Mains AC is
 * handled separately via the ac_meter module (true windowed RMS). */
static const uint8_t term_mv_chan[] = {
	AIN_ADC_PDC0, AIN_ADC_PDC1, AIN_ADC_PDC2, AIN_ADC_PDC3,
	AIN_ADC_PDC4, AIN_ADC_PDC5, AIN_ADC_PDC6, AIN_ADC_PDC7,
	AIN_ADC_PDC0_ALT,
	AIN_ADC_12V, AIN_ADC_5V0, AIN_ADC_3V3,
};

static uint32_t last_mv[ARRAY_SIZE(term_mv_chan)];  /* last printed rail values */
static int16_t  last_t1;
static int16_t  last_t2;
/* SENSOR log: 0 = off (default; use the temp/sensor commands instead),
 * 1 = print the boot snapshot and value changes. */
#ifndef TERMINAL_SENSOR_LOG
#define TERMINAL_SENSOR_LOG 0
#endif

static bool     inited;

/* last printed AC mains state (ac_meter) */
static uint32_t last_ac_rms;
static int16_t  last_ac_freq;
static bool     last_ac_present;
static bool     ac_inited;

static void printAc(void)
{
	uint32_t rms = acMeterGetVinRmsMv();
	int16_t  f   = acMeterGetVinFreq();
	bool     p   = acMeterAcPresent();

#if TERMINAL_SENSOR_LOG
	if (!p) {
		printk("SENSOR adc_vin: n/a (no AC)\n");
	} else {
		printk("SENSOR adc_vin: %u.%03u V @ %u.%u Hz\n",
		       rms / 1000U, rms % 1000U, f / 10, f % 10);
	}
#endif
	last_ac_rms     = rms;
	last_ac_freq    = f;
	last_ac_present = p;
}

/* Print AC mains only when the presence, voltage, or frequency moved enough. */
static void updateAc(void)
{
	uint32_t rms = acMeterGetVinRmsMv();
	int16_t  f   = acMeterGetVinFreq();
	bool     p   = acMeterAcPresent();

	if (!ac_inited) {
		ac_inited = true;
		printAc();
		return;
	}

	bool print = false;
	if (p != last_ac_present) {
		print = true;
	} else if (p) {
		uint32_t d  = (rms > last_ac_rms) ? (rms - last_ac_rms) : (last_ac_rms - rms);
		int32_t  df = (int32_t)f - last_ac_freq;
		if (df < 0) df = -df;
		if (d >= TERMINAL_MV_DELTA || df >= TERMINAL_FREQ_DELTA) {
			print = true;
		}
	}

	if (print) {
		printAc();
	}
}

#if TERMINAL_SENSOR_LOG
static void printTemp(const char *name, int16_t t)
{
	if (t == INT16_MIN) {
		printk("SENSOR %s: FAULT\n", name);
	} else if (t == 0) {
		printk("SENSOR %s: n/a\n", name);
	} else {
		/* Handle negative temperatures correctly: -334 -> "-33.4" */
		int16_t neg = (t < 0);
		int16_t a = neg ? (int16_t)-t : t;
		printk("SENSOR %s: %s%d.%d °C\n", name, neg ? "-" : "", a / 10, a % 10);
	}
}
#endif

bool terminalIsQuiet(void)
{
	return uartCmdDfuActive();
}

void terminalUpdate(void)
{
	/* The host command service is polled by the scheduler's 10 ms thread;
	 * uartCmdPoll() is not called here (500 ms is too slow for DFU). */

	/* Stay quiet during DFU: SENSOR printk shares USART1 with the ACK stream. */
	if (terminalIsQuiet()) {
		return;
	}

	int16_t t1 = sensorTempGet1();
	int16_t t2 = sensorTempGet2();

	if (!inited) {
		/* Boot snapshot: only record the initial values (no boot-time spam) */
#if TERMINAL_SENSOR_LOG
		printTemp("temp1", t1);
		printTemp("temp2", t2);
#endif
		for (size_t i = 0; i < ARRAY_SIZE(term_mv_chan); i++) {
			last_mv[i] = sensorGetPhys(term_mv_chan[i]);
#if TERMINAL_SENSOR_LOG
			printk("SENSOR %s: %u.%03u V\n",
			       bspAinGetName(term_mv_chan[i]),
			       last_mv[i] / 1000U, last_mv[i] % 1000U);
#endif
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
#if TERMINAL_SENSOR_LOG
		printTemp("temp1", t1);
#endif
		last_t1 = t1;
	}
	if (dt2 >= TERMINAL_TEMP_DELTA && t2 > 0) {
#if TERMINAL_SENSOR_LOG
		printTemp("temp2", t2);
#endif
		last_t2 = t2;
	}

	/* voltage — print when it moved > 3 V */
	for (size_t i = 0; i < ARRAY_SIZE(term_mv_chan); i++) {
		uint32_t mv = sensorGetPhys(term_mv_chan[i]);
		uint32_t d = (mv > last_mv[i]) ? (mv - last_mv[i]) : (last_mv[i] - mv);
		if (d >= TERMINAL_MV_DELTA) {
#if TERMINAL_SENSOR_LOG
			printk("SENSOR %s: %u.%03u V\n",
			       bspAinGetName(term_mv_chan[i]),
			       mv / 1000U, mv % 1000U);
#endif
			last_mv[i] = mv;
		}
	}

	/* AC mains (ac_meter) — presence / RMS / frequency */
	updateAc();
}
