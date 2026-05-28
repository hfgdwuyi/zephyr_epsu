/*!
 * Copyright � Siemens Healthcare GmbH 2022, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 * @file
 * @brief Cyclic Redundancy Check calculation functions
 */
/*----------------------------------------------------------------------------*/
#include <stdbool.h>
#include <stddef.h>  
#include <stdint.h>
// Project includes
#include "crc_calc.h"

#include <zephyr/device.h>
#include <zephyr/drivers/crc.h>
#include <zephyr/sys/util.h>

#define CRC_NODE DT_CHOSEN(zephyr_crc)

static const struct device *crc_dev;

int crcInit(void)
{
    crc_dev = DEVICE_DT_GET_OR_NULL(CRC_NODE);
    if (!crc_dev) {
        return -ENODEV;
    }
    if (!device_is_ready(crc_dev)) {
        return -ENODEV;
    }
    return 0;
}

int crcReset(void)
{
    /* No persistent HW state is kept in this wrapper; ctx drives per-call state. */
    if (!crc_dev || !device_is_ready(crc_dev)) {
        return -ENODEV;
    }
    return 0;
}

int crcCalc8(const void *data, size_t len, uint8_t *out)
{
    if (!out || (!data && len)) {
        return -EINVAL;
    }
    if (!crc_dev || !device_is_ready(crc_dev)) {
        return -ENODEV;
    }

#if !IS_ENABLED(CONFIG_CRC_DRIVER_HAS_CRC8)
    return -ENOTSUP;
#else
    struct crc_ctx ctx = {
        .type = CRC8,
        .polynomial = CRC8_POLY,
        .seed = CRC8_INIT_VAL,
        .reversed = (CRC_FLAG_REVERSE_OUTPUT | CRC_FLAG_REVERSE_INPUT),
    };

    int ret = crc_begin(crc_dev, &ctx);
    if (ret) {
        return ret;
    }

    ret = crc_update(crc_dev, &ctx, data, len);
    if (ret) {
        return ret;
    }

    ret = crc_finish(crc_dev, &ctx);
    if (ret) {
        return ret;
    }

    *out = (uint8_t)ctx.result;
    return 0;
#endif
}

int crcCalc32(const void *data, size_t len, uint32_t *out)
{
    if (!out || (!data && len)) {
        return -EINVAL;
    }
    if (!crc_dev || !device_is_ready(crc_dev)) {
        return -ENODEV;
    }

#if !IS_ENABLED(CONFIG_CRC_DRIVER_HAS_CRC32_IEEE)
    return -ENOTSUP;
#else
    struct crc_ctx ctx = {
        .type = CRC32_IEEE,
        .polynomial = CRC32_IEEE_POLY,
        .seed = CRC32_IEEE_INIT_VAL,
        .reversed = (CRC_FLAG_REVERSE_OUTPUT | CRC_FLAG_REVERSE_INPUT),
    };

    int ret = crc_begin(crc_dev, &ctx);
    if (ret) {
        return ret;
    }

    ret = crc_update(crc_dev, &ctx, data, len);
    if (ret) {
        return ret;
    }

    ret = crc_finish(crc_dev, &ctx);
    if (ret) {
        return ret;
    }

    *out = (uint32_t)ctx.result;
    return 0;
#endif
}