#include "bsp_ain.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/adc.h>

#if DT_NODE_HAS_STATUS(DT_PATH(zephyr_user), okay) && DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)

#define AIN_IO_CHANNELS_NODE DT_PATH(zephyr_user)
#define AIN_NUMBER DT_PROP_LEN(AIN_IO_CHANNELS_NODE, io_channels)

#define AIN_ADC_SPEC_ELEM(node_id, prop, idx) ADC_DT_SPEC_GET_BY_IDX(node_id, idx)

static const struct adc_dt_spec ain_specs[AIN_NUMBER] = {
    DT_FOREACH_PROP_ELEM_SEP(AIN_IO_CHANNELS_NODE, io_channels, AIN_ADC_SPEC_ELEM, (,))
};

static uint32_t ainData[AIN_NUMBER];

/* Names follow HAL mapping by index (matches io-channels order in app.overlay) */
static const char *const ainName[AIN_NUMBER] = {
    /* idx: 0..7 */
    "PUMP-ADC",       /* 0 */
    "FORCE-ADC(1)",   /* 1 */
    "FORCE-ADC(2)",   /* 2 */
    "FORCE-ADC(3)",   /* 3 */
    "TEMP-FD-ADC",    /* 4 */
    "TEMP-MB-ADC",    /* 5 */
    "ADC1_INP12",     /* 6 */
    "ADC3_INP1"       /* 7 */
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

    /* 2) print all sampled values every poll */
    printk("AIN: samples:");
    for (size_t i = 0; i < AIN_NUMBER; i++) {
        printk(" [%u]%s=%u",
               (unsigned)i,
               (i < ARRAY_SIZE(ainName) && ainName[i]) ? ainName[i] : "(unnamed)",
               (unsigned)ainData[i]);
    }
    printk("\n");
}

uint32_t bspAinGetRawValue(uint8_t channel)
{
    if (channel >= AIN_NUMBER) {
        return 0;
    }
    return ainData[channel];
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

#endif