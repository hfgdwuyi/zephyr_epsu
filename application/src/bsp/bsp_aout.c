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

    printk("bspAoutInit: ok dev=%s ch=%u res=%u\n",
           dac_dev->name, (unsigned)BSP_DAC_CHANNEL_ID, (unsigned)BSP_DAC_RESOLUTION);
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

/* ---- pwr_on_off: DAC1_OUT2 (PA5), 0-1.5 V @ 0.25 Hz square wave ----
 * Driven from bspAoutPoll() while the channel's state bit is active.
 * Uses a boot-time clock so the phase stays continuous across state
 * transitions while active. */

typedef enum {
	PWR_ON_OFF_LEVEL_MV   = 1500,
	PWR_ON_OFF_PERIOD_MS  = 4000,   /* 0.25 Hz */
	PWR_ON_OFF_HALF_MS    = PWR_ON_OFF_PERIOD_MS / 2,
} pwrOnOffWave_t;

static bool aout_state[AOUT_CH_COUNT];

void bspAoutSetState(uint8_t channel, bool active)
{
    if (channel >= AOUT_CH_COUNT) {
        return;
    }
    aout_state[channel] = active;
    if (!active) {
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
    /* pwr_on_off square wave */
    if (aout_state[AOUT_PWR_ON_OFF]) {
        const uint32_t tick = k_uptime_get_32();
        const bool high = (tick % PWR_ON_OFF_PERIOD_MS) < PWR_ON_OFF_HALF_MS;
        bspAoutWrite(AOUT_PWR_ON_OFF, high ? PWR_ON_OFF_LEVEL_MV : 0);
    }
}
