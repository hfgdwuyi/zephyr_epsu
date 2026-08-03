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
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/sys/printk.h>

#include <stdbool.h>
#include <stdint.h>

#include "bsp_aout.h"

/*
 * Zephyr port notes:
 * - DAC peripheral is enabled/configured by devicetree + driver.
 * - This module exposes a legacy API: bspAoutInit() + bspAoutWrite().
 *
 * Value convention:
 *   bspAoutWrite(channel, val) where val is in millivolts (mV).
 */

/* Read DAC configuration from /zephyr,user (same pattern as Zephyr DAC sample) */
#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

#if (DT_NODE_HAS_STATUS(ZEPHYR_USER_NODE, okay) && \
     DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, dac) && \
     DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, dac_channel_id) && \
     DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, dac_resolution))
#define BSP_DAC_NODE       DT_PHANDLE(ZEPHYR_USER_NODE, dac)
#define BSP_DAC_CHANNEL_ID DT_PROP(ZEPHYR_USER_NODE, dac_channel_id)
#define BSP_DAC_RESOLUTION DT_PROP(ZEPHYR_USER_NODE, dac_resolution)
#define BSP_AOUT_ENABLED 1
#else
#define BSP_AOUT_ENABLED 0
#endif

#if BSP_AOUT_ENABLED

static const struct device *const dac_dev = DEVICE_DT_GET(BSP_DAC_NODE);

#define BSP_AOUT_CHANNEL_COUNT 1U
#define BSP_AOUT_MAX_CODE ((1U << BSP_DAC_RESOLUTION) - 1U)

#ifndef BSP_AOUT_VREF_MV
#define BSP_AOUT_VREF_MV 3300U
#endif

static bool aout_ready;

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
}

#else

void bspAoutInit(void)
{
    printk("bspAoutInit: DAC not configured (no dac in /zephyr,user)\n");
}

void bspAoutWrite(uint8_t channel, int16_t val)
{
    ARG_UNUSED(channel);
    ARG_UNUSED(val);
}

#endif
