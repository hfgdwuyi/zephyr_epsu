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
/* SENSOR 周期/变化日志开关：
 * 0 = 关闭（默认，串口保持干净；需要看值时用 temp / sensor 命令查询）
 * 1 = 打印启动快照及变化触发的传感器值 */
#ifndef TERMINAL_SENSOR_LOG
#define TERMINAL_SENSOR_LOG 0
#endif

/* 周期主动推送全部 ADC 通道：
 * 1 = 默认开启（上位机无需发送任何命令即可持续接收全部通道数据）
 * 0 = 关闭（仅在收到 ain 命令时输出） */
#ifndef TERMINAL_AIN_PUSH
#define TERMINAL_AIN_PUSH 1
#endif
#define AIN_PUSH_PERIOD_MS  1000

static uint32_t last_ain_push;
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

static void printTemp(const char *name, int16_t t)
{
	if (t == INT16_MIN) {
		printk("SENSOR %s: FAULT\n", name);
	} else if (t == 0) {
		printk("SENSOR %s: n/a\n", name);
	} else {
		/* 正确处理负温度：-334 → "-33.4"（原实现会打成 "-33.-4"） */
		int16_t neg = (t < 0);
		int16_t a = neg ? (int16_t)-t : t;
		printk("SENSOR %s: %s%d.%d °C\n", name, neg ? "-" : "", a / 10, a % 10);
	}
}

/* 周期性输出全部 ADC 通道 —— 沿用上位机既有的 SENSOR 行格式：
 *   SENSOR adc_12v: 1.414 V
 *   SENSOR temp1: 25.3 °C
 *   SENSOR adc_vin: 220.5 V @ 50.0 Hz   /   n/a (no AC)
 * 与旧实现的区别：不再"仅变化 >3V 才打印"（那样 0V 附近的 pdc 通道永远
 * 不上报，上位机只能显示 n/a），而是每个周期把 15 个通道全部上报一次。 */
static void printAllAin(void)
{
	/* 温度通道（名字与旧格式保持一致：temp1 / temp2） */
	printTemp("temp1", sensorTempGet1());
	printTemp("temp2", sensorTempGet2());

	/* 其余电压通道（跳过温度与市电 AC 通道，它们单独处理） */
	for (uint8_t i = 0; i < BSP_AIN_NUMBER; i++) {
		if (i == AIN_ADC_TEMP1 || i == AIN_ADC_TEMP2 || i == AIN_ADC_VIN) {
			continue;
		}
		const uint32_t mv = sensorGetPhys(i);
		printk("SENSOR %s: %u.%03u V\n",
		       bspAinGetName(i), mv / 1000U, mv % 1000U);
	}

	/* 市电 AC 通道：由 ac_meter 提供窗口 RMS 与频率 */
	if (acMeterAcPresent()) {
		const uint32_t rms = acMeterGetVinRmsMv();
		printk("SENSOR adc_vin: %u.%03u V @ %u.%u Hz\n",
		       rms / 1000U, rms % 1000U,
		       acMeterGetVinFreq() / 10, acMeterGetVinFreq() % 10);
	} else {
		printk("SENSOR adc_vin: n/a (no AC)\n");
	}
}

bool terminalIsQuiet(void)
{
	return uartCmdDfuActive();
}

void terminalUpdate(void)
{
	/* 上位机命令服务（USART1）由 scheduler 的 10 ms cmd 线程轮询，
	 * 此处不再调用 uartCmdPoll()（500ms 周期太慢，DFU 时 RX 会溢出）。 */

	/* DFU 上传期间静默：SENSOR printk 与升级 ACK 共用 USART1，
	 * 打印会抢占总线导致主机收不到完整的 "ok" 应答。 */
	if (terminalIsQuiet()) {
		return;
	}

	int16_t t1 = sensorTempGet1();
	int16_t t2 = sensorTempGet2();

	if (!inited) {
		/* 启动快照：仅记录初值，不打印（避免开机 15 行刷屏） */
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

#if TERMINAL_AIN_PUSH
	/* 周期性推送全部 ADC 通道：上位机默认即可收到所有通道，无需发命令 */
	const uint32_t now = k_uptime_get_32();
	if ((uint32_t)(now - last_ain_push) >= AIN_PUSH_PERIOD_MS) {
		last_ain_push = now;
		printAllAin();
	}
#endif
}
