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

/* Standard library */
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

/* Verbose init log: 0 = off (default), 1 = print */
#ifndef BSP_AOUT_VERBOSE_LOG
#define BSP_AOUT_VERBOSE_LOG 0
#endif

static bool aout_ready;
static int16_t last_dac_mv;   /* last bspAoutWrite() value (mV), for status query */

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

    /* Remember the last written value (for host status queries) */
    last_dac_mv = (int16_t)mv;
}

int16_t bspAoutGetMv(uint8_t channel)
{
    if (channel >= AOUT_CH_COUNT) {
        return 0;
    }
    return last_dac_mv;
}

/* NOTE: indicator logic (breath/on/off) moved to the application indicator
 *       module; the BSP only provides DAC primitives. */
