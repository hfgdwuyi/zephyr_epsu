/*!
 * Copyright Siemens Healthcare GmbH 2022, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 * @file
 * @brief Analog output writing (Zephyr port)
 *
 */
/*----------------------------------------------------------------------------*/
/* C standard library */
#include <stdbool.h>
#include <stdint.h>

/* Zephyr */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/sys/printk.h>

/* BSP */
#include "bsp_aout.h"

/*
 * Zephyr port notes:
 * - DAC peripheral is enabled/configured by devicetree + driver.
 * - This module exposes a legacy API: bspAoutInit() + bspAoutWrite().
 *
 * Value convention:
 *   bspAoutWrite(channel, val) where val is in millivolts (mV).
 */

/* DAC is fixed on this platform (H745) — always present in devicetree. */
#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

#define BSP_DAC_NODE       DT_PHANDLE(ZEPHYR_USER_NODE, dac)
#define BSP_DAC_CHANNEL_ID DT_PROP(ZEPHYR_USER_NODE, dac_channel_id)
#define BSP_DAC_RESOLUTION DT_PROP(ZEPHYR_USER_NODE, dac_resolution)

static const struct device *const dac_dev = DEVICE_DT_GET(BSP_DAC_NODE);

#define BSP_AOUT_CHANNEL_COUNT 1U
#define BSP_AOUT_MAX_CODE ((1U << BSP_DAC_RESOLUTION) - 1U)

#ifndef BSP_AOUT_VREF_MV
#define BSP_AOUT_VREF_MV 3300U
#endif

/* 初始化明细日志开关：0 = 关闭（默认，串口保持干净）；1 = 打印 */
#ifndef BSP_AOUT_VERBOSE_LOG
#define BSP_AOUT_VERBOSE_LOG 0
#endif

static bool aout_ready;
static int16_t last_dac_mv;   /* 最近一次 bspAoutWrite 的电压值 (mV) — 状态查询用 */

static const struct dac_channel_cfg dac_ch_cfg = {
    .channel_id = BSP_DAC_CHANNEL_ID,
    .resolution = BSP_DAC_RESOLUTION,
#if defined(CONFIG_DAC_BUFFER_NOT_SUPPORT)
    .buffered = false,
#else
    .buffered = true,
#endif
};

void bspAoutInit(void)
{
    aout_ready = false;

    if (!device_is_ready(dac_dev)) {
        printk("bspAoutInit: DAC device not ready\n");
        return;
    }

    int ret = dac_channel_setup(dac_dev, &dac_ch_cfg);
    if (ret != 0) {
        printk("bspAoutInit: dac_channel_setup(ch=%u,res=%u) failed: %d\n",
               (unsigned)BSP_DAC_CHANNEL_ID, (unsigned)BSP_DAC_RESOLUTION, ret);
        return;
    }

    aout_ready = true;

#if BSP_AOUT_VERBOSE_LOG
    printk("bspAoutInit: ok dev=%s ch=%u res=%u\n",
           dac_dev->name, (unsigned)BSP_DAC_CHANNEL_ID, (unsigned)BSP_DAC_RESOLUTION);
#endif
}

void bspAoutWrite(uint8_t channel, int16_t val)
{
    if (!aout_ready) {
        return;
    }

    if (channel >= BSP_AOUT_CHANNEL_COUNT) {
        return;
    }

    int32_t mv = (int32_t)val;
    if (mv < 0) {
        mv = 0;
    }
    if (mv > (int32_t)BSP_AOUT_VREF_MV) {
        mv = (int32_t)BSP_AOUT_VREF_MV;
    }

    const uint32_t code = (uint32_t)(((uint64_t)mv * (uint64_t)BSP_AOUT_MAX_CODE) / BSP_AOUT_VREF_MV);
    (void)dac_write_value(dac_dev, BSP_DAC_CHANNEL_ID, code);

    /* 记录最近一次写入值（供上位机状态查询） */
    last_dac_mv = (int16_t)mv;
}

int16_t bspAoutGetMv(uint8_t channel)
{
    if (channel >= AOUT_CH_COUNT) {
        return 0;
    }
    return last_dac_mv;
}

/* ---- pwr_on_off: DAC1_OUT2 (PA5) — 0-1.5 V 三角波 @ 0.25 Hz ----
 *
 * 上电即启动（独立于状态机）。一个周期 4000 ms：
 *   前半周期 (0..2000 ms)   : 0 V → 1.5 V 线性上升（上升锯齿）
 *   后半周期 (2000..4000 ms): 1.5 V → 0 V 线性下降（下降锯齿）
 * 首尾相接连续往复，无跳变。
 *
 * 相位由 k_uptime_get_32() 推算，因此即使 poll 周期变化或线程被延迟，
 * 波形频率与连续性也不受影响（不会累积漂移）。 */

typedef enum {
	TRI_MAX_MV    = 1500,   /* 峰值 (mV) */
	TRI_PERIOD_MS = 4000,   /* 0.25 Hz */
	TRI_HALF_MS   = 2000,   /* 半周期：上升段 / 下降段各 2 s */
} triWave_t;

static bool aout_state[AOUT_CH_COUNT];
static bool tri_enabled = true;   /* 上电默认启动三角波 */

void bspAoutSetTriangleEnabled(bool en)
{
	tri_enabled = en;
	if (!en) {
		bspAoutWrite(AOUT_PWR_ON_OFF, 0);
	}
}

bool bspAoutGetTriangleEnabled(void)
{
	return tri_enabled;
}

void bspAoutSetState(uint8_t channel, bool active)
{
    if (channel >= AOUT_CH_COUNT) {
        return;
    }
    aout_state[channel] = active;
    if (!active && channel != AOUT_PWR_ON_OFF) {
        /* pwr_on_off (PA5) 由三角波发生器独占驱动，状态机不强制归零，
         * 否则会在状态切换瞬间产生 0 V 尖峰。 */
        bspAoutWrite(channel, 0);
    }
}

bool bspAoutGetState(uint8_t channel)
{
    if (channel >= AOUT_CH_COUNT) {
        return false;
    }
    return aout_state[channel];
}

void bspAoutPoll(void)
{
	/* pwr_on_off: 0-1.5 V 三角波（上升锯齿 + 下降锯齿往复） */
	if (!tri_enabled) {
		return;
	}

	const uint32_t phase = k_uptime_get_32() % (uint32_t)TRI_PERIOD_MS;
	int32_t mv;

	if (phase < (uint32_t)TRI_HALF_MS) {
		/* 上升段：phase 0 → 2000 对应 0 → 1500 mV */
		mv = ((int32_t)TRI_MAX_MV * (int32_t)phase) / (int32_t)TRI_HALF_MS;
	} else {
		/* 下降段：phase 2000 → 4000 对应 1500 → 0 mV */
		mv = ((int32_t)TRI_MAX_MV * (int32_t)(TRI_PERIOD_MS - (int32_t)phase))
		     / (int32_t)TRI_HALF_MS;
	}

	bspAoutWrite(AOUT_PWR_ON_OFF, (int16_t)mv);
}
