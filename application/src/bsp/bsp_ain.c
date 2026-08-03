#include "bsp_ain.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bsp_ain, LOG_LEVEL_INF);

#if DT_NODE_HAS_STATUS(DT_PATH(zephyr_user), okay) && DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)

#define AIN_IO_CHANNELS_NODE DT_PATH(zephyr_user)
#define AIN_NUMBER DT_PROP_LEN(AIN_IO_CHANNELS_NODE, io_channels)

#define AIN_ADC_SPEC_ELEM(node_id, prop, idx) ADC_DT_SPEC_GET_BY_IDX(node_id, idx)

static const struct adc_dt_spec ain_specs[AIN_NUMBER] = {
    DT_FOREACH_PROP_ELEM_SEP(AIN_IO_CHANNELS_NODE, io_channels, AIN_ADC_SPEC_ELEM, (,))
};

static uint32_t ainData[AIN_NUMBER];

/* Names follow pin_config.xlsx order (matches io-channels order in app.overlay) */
static const char *const ainName[AIN_NUMBER] = {
    "adc_3v3",         /* 0  - PF6 */
    "adc_pdc1",        /* 1  - PF7 */
    "adc_pdc7",        /* 2  - PF8 */
    "adc_pdc6",        /* 3  - PF9 */
    "adc_pdc5",        /* 4  - PF10 */
    "adc_temp1",       /* 5  - PA3 */
    "adc_temp2",       /* 6  - PA4 */
    "adc_pdc0",        /* 7  - PA6 */
    "adc_pdc4",        /* 8  - PC0 */
    "adc_pdc2",        /* 9  - PB0 */
    "adc_pdc3",        /* 10 - PB1 */
    "adc_vin",         /* 11 - PC2 */
    "adc_pdc0_alt",    /* 12 - PC3 (second PDC0 channel) */
    "adc_5v",          /* 13 - PH4 */
};

void bspAinInit(void)
{
    for (size_t i = 0; i < AIN_NUMBER; i++) {
        ainData[i] = 0;
    }

    /* Setup each channel (best-effort; some drivers may return -ENOTSUP) */
    for (size_t i = 0; i < AIN_NUMBER; i++) {
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
    printk("AIN: init done (poll), inputs=%u\n", (unsigned)AIN_NUMBER);
    for (size_t i = 0; i < AIN_NUMBER; i++) {
        const struct adc_dt_spec *spec = &ain_specs[i];
        printk("AIN[%u]: name=%s dev=%s ch=%u\n",
               (unsigned)i,
               (i < ARRAY_SIZE(ainName) && ainName[i]) ? ainName[i] : "(unnamed)",
               spec->dev ? spec->dev->name : "(null)",
               (unsigned)spec->channel_id);
    }
}

/* Call this periodically (e.g., 20ms/50ms/100ms/1000ms) to refresh all channels */
/* Call this periodically (e.g., 20ms/50ms/100ms/1000ms) to refresh all channels */
void bspAinPoll(void)
{
    /* 1) sample all channels */
    for (size_t i = 0; i < AIN_NUMBER; i++) {
        const struct adc_dt_spec *spec = &ain_specs[i];

        if (!adc_is_ready_dt(spec) || spec->dev == NULL) {
            printk("AIN[%u]: not ready dev=%s\n",
                   (unsigned)i, spec->dev ? spec->dev->name : "(null)");
            continue;
        }

        /* This simple implementation supports channel_id < 32 (BIT mask) */
        if (spec->channel_id >= 32U) {
            printk("AIN[%u]: invalid channel_id=%u (dev=%s)\n",
                   (unsigned)i, (unsigned)spec->channel_id,
                   spec->dev ? spec->dev->name : "(null)");
            continue;
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
            ainData[i] = (uint16_t)sample;
        } else {
            printk("AIN[%u]: read failed name=%s dev=%s ch=%u ret=%d\n",
                   (unsigned)i,
                   (i < ARRAY_SIZE(ainName) && ainName[i]) ? ainName[i] : "(unnamed)",
                   spec->dev ? spec->dev->name : "(null)",
                   (unsigned)spec->channel_id,
                   ret);
            /* Keep previous value in ainData[i] */
        }
    }

    /* 2) log all sampled values every poll (DBG level for production) */
    LOG_DBG("AIN: samples:");
    for (size_t i = 0; i < AIN_NUMBER; i++) {
        LOG_DBG(" [%u]%s=%u",
                (unsigned)i,
                (i < ARRAY_SIZE(ainName) && ainName[i]) ? ainName[i] : "(unnamed)",
                (unsigned)ainData[i]);
    }
}

uint32_t bspAinGetRawValue(uint8_t channel)
{
    if (channel >= AIN_NUMBER) {
        return 0;
    }
    return ainData[channel];
}

uint32_t bspAinReadMv(uint8_t channel)
{
    uint32_t raw = bspAinGetRawValue(channel);
    return (raw * 3300U) / 4095U;
}

uint32_t bspAinReadDivMv(uint8_t channel, uint32_t rHigh, uint32_t rLow)
{
    uint32_t vAdc = bspAinReadMv(channel);
    return (vAdc * (rHigh + rLow)) / rLow;
}

/*
 * Mains AC voltage at AIN_ADC_VIN (channel 11).
 * Vadc = 1.65 + (0.4 x 1.414 x Vin x 50.2 / 3575) x 3.9 / 6.49
 *   => Vadc = 1.65 + Vin x 0.004773
 *   => Vin  = (Vadc - 1.65) / 0.004773
 * All in mV.  Returns 0 if Vadc < 1.65 V.
 */
uint32_t bspAinReadVinMv(void)
{
    int32_t vAdc = (int32_t)bspAinReadMv(AIN_ADC_VIN);
    int32_t delta = vAdc - 1650;
    if (delta <= 0) {
        return 0;
    }
    return (uint32_t)(delta * 20953U / 100U);
}

const char *bspAinGetName(uint8_t channel)
{
    if (channel >= AIN_NUMBER) {
        return NULL;
    }
    return (channel < ARRAY_SIZE(ainName)) ? ainName[channel] : NULL;
}

#else

void bspAinInit(void)
{
    printk("AIN: disabled (no /zephyr,user io-channels)\n");
}

void bspAinPoll(void)
{
    /* no-op */
}

uint32_t bspAinGetRawValue(uint8_t channel)
{
    ARG_UNUSED(channel);
    return 0;
}

const char *bspAinGetName(uint8_t channel)
{
    ARG_UNUSED(channel);
    return NULL;
}

uint32_t bspAinReadVinMv(void)            { return 0; }

#endif