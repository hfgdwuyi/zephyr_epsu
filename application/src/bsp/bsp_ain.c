/* Zephyr */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

/* BSP */
#include "bsp_ain.h"

LOG_MODULE_REGISTER(bsp_ain, LOG_LEVEL_INF);

#define AIN_ADC_SPEC_ELEM(node_id, prop, idx) ADC_DT_SPEC_GET_BY_IDX(node_id, idx)

static const struct adc_dt_spec ain_specs[BSP_AIN_NUMBER] = {
    DT_FOREACH_PROP_ELEM_SEP(BSP_AIN_IO_CHANNELS_NODE, io_channels, AIN_ADC_SPEC_ELEM, (,))
};

static uint32_t ainData[BSP_AIN_NUMBER];

/* Names follow pin_config.xlsx order (matches io-channels order in app.overlay) */
static const char *const ainName[BSP_AIN_NUMBER] = {
    "adc_12v",         /* 0  - PH2   */
    "adc_pdc7",        /* 1  - PF11  */
    "adc_pdc6",        /* 2  - PF12  */
    "adc_pdc5",        /* 3  - PF13  */
    "adc_5v0",         /* 4  - PF14  */
    "adc_temp1",       /* 5  - PA3   */
    "adc_temp2",       /* 6  - PA4   */
    "adc_pdc0",        /* 7  - PA6   */
    "adc_pdc4",        /* 8  - PA0_C */
    "adc_pdc2",        /* 9  - PB0   */
    "adc_pdc3",        /* 10 - PB1   */
    "adc_3v3",         /* 11 - PC0   */
    "adc_pdc1",        /* 12 - PC2   */
    "adc_vin",         /* 13 - PC2_C */
    "adc_pdc0_alt",    /* 14 - PC3_C */
};

void bspAinInit(void)
{
    for (size_t i = 0; i < BSP_AIN_NUMBER; i++) {
        ainData[i] = 0;
    }

    /* Setup each channel (best-effort; some drivers may return -ENOTSUP) */
    for (size_t i = 0; i < BSP_AIN_NUMBER; i++) {
        const struct adc_dt_spec *spec = &ain_specs[i];

        if (!adc_is_ready_dt(spec)) {
            printk("AIN[%u]: ADC device not ready: %s\n",
                   (unsigned)i, spec->dev ? spec->dev->name : "(null)");
            continue;
        }

        int ret = adc_channel_setup_dt(spec);
        if (ret != 0 && ret != -ENOTSUP) {
            printk("AIN[%u]: adc_channel_setup_dt failed (%d) dev=%s ch=%u\n",
                   (unsigned)i, ret,
                   spec->dev ? spec->dev->name : "(null)",
                   (unsigned)spec->channel_id);
            continue;
        }
    }

    /* Print summary */
    printk("AIN: init done (poll), inputs=%u\n", (unsigned)BSP_AIN_NUMBER);
    for (size_t i = 0; i < BSP_AIN_NUMBER; i++) {
        const struct adc_dt_spec *spec = &ain_specs[i];
        printk("AIN[%u]: name=%s dev=%s ch=%u\n",
               (unsigned)i,
               (i < ARRAY_SIZE(ainName) && ainName[i]) ? ainName[i] : "(unnamed)",
               spec->dev ? spec->dev->name : "(null)",
               (unsigned)spec->channel_id);
    }
}

/*
 * Blocking single-channel read into the ainData[] snapshot.
 * Returns 0 on success, negative errno on failure (previous value kept).
 */
static int ainReadChannel(size_t idx)
{
    const struct adc_dt_spec *spec = &ain_specs[idx];

    if (!adc_is_ready_dt(spec) || spec->dev == NULL) {
        printk("AIN[%u]: not ready dev=%s\n",
               (unsigned)idx, spec->dev ? spec->dev->name : "(null)");
        return -ENODEV;
    }

    /* This simple implementation supports channel_id < 32 (BIT mask) */
    if (spec->channel_id >= 32U) {
        printk("AIN[%u]: invalid channel_id=%u (dev=%s)\n",
               (unsigned)idx, (unsigned)spec->channel_id,
               spec->dev ? spec->dev->name : "(null)");
        return -EINVAL;
    }

    int16_t sample = 0;

    struct adc_sequence seq = {
        .channels = BIT(spec->channel_id),
        .buffer = &sample,
        .buffer_size = sizeof(sample),
        .resolution = 12,
    };

    /* Optional: let DT refine sequence if supported */
    int ret = adc_sequence_init_dt(spec, &seq);
    if (ret != 0 && ret != -ENOTSUP) {
        /* keep going with minimal sequence */
    }

    ret = adc_read(spec->dev, &seq);
    if (ret == 0) {
        ainData[idx] = (uint16_t)sample;
        return 0;
    }

    printk("AIN[%u]: read failed name=%s dev=%s ch=%u ret=%d\n",
           (unsigned)idx,
           (idx < ARRAY_SIZE(ainName) && ainName[idx]) ? ainName[idx] : "(unnamed)",
           spec->dev ? spec->dev->name : "(null)",
           (unsigned)spec->channel_id,
           ret);
    /* Keep previous value in ainData[idx] */
    return ret;
}

/* Call this periodically (e.g., 20ms/50ms/100ms/1000ms) to refresh all channels */
void bspAinPoll(void)
{
    /* 1) sample all channels except AIN_ADC_VIN — the ac_meter module owns
     * that channel exclusively via bspAinReadRaw() at a high rate. */
    for (size_t i = 0; i < BSP_AIN_NUMBER; i++) {
        if (i == AIN_ADC_VIN) {
            continue;
        }
        (void)ainReadChannel(i);
    }

    /* 2) log all sampled values every poll (DBG level for production) */
    LOG_DBG("AIN: samples:");
    for (size_t i = 0; i < BSP_AIN_NUMBER; i++) {
        LOG_DBG(" [%u]%s=%u",
                (unsigned)i,
                (i < ARRAY_SIZE(ainName) && ainName[i]) ? ainName[i] : "(unnamed)",
                (unsigned)ainData[i]);
    }
}

/* One-shot blocking read of a single channel (bypasses the poll snapshot).
 * Returns the raw 12-bit count (0–4095), or 0 on failure. */
uint32_t bspAinReadRaw(uint8_t channel)
{
    if (channel >= BSP_AIN_NUMBER) {
        return 0;
    }
    if (ainReadChannel(channel) != 0) {
        return 0;
    }
    return ainData[channel];
}

uint32_t bspAinGetRawValue(uint8_t channel)
{
    if (channel >= BSP_AIN_NUMBER) {
        return 0;
    }
    return ainData[channel];
}

const char *bspAinGetName(uint8_t channel)
{
    if (channel >= BSP_AIN_NUMBER) {
        return NULL;
    }
    return (channel < ARRAY_SIZE(ainName)) ? ainName[channel] : NULL;
}