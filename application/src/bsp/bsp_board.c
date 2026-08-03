/*!
 * Copyright © Siemens Healthcare GmbH 2023, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 * @file
 * @brief Pins and interfaces initialization (Zephyr port) — cios-zhong
 */
/*----------------------------------------------------------------------------*/

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>

#include <stdbool.h>
#include <stdint.h>

#include "bsp_led.h"
#include "bsp_board.h"
#include "bsp_ain.h"
#include "bsp_aout.h"
#include "bsp_dio.h"
#include "bsp_pwm.h"

#if DT_NODE_HAS_STATUS(DT_ALIAS(i2c0), okay)
static const struct device *const i2c0_dev = DEVICE_DT_GET_OR_NULL(DT_ALIAS(i2c0));
#else
static const struct device *const i2c0_dev = NULL;
#endif

#if DT_NODE_HAS_STATUS(DT_ALIAS(spi0), okay)
static const struct device *const spi0_dev = DEVICE_DT_GET_OR_NULL(DT_ALIAS(spi0));
#else
static const struct device *const spi0_dev = NULL;
#endif

/*----------------------------------------------------------------------------*/
/*! @brief Pins / peripherals initialization (Zephyr) */
/*----------------------------------------------------------------------------*/
static void bspDinApplyDebounce(void);

void boardInit(void)
{
	bspDioInit();       /* DOUT config + DIN init + 1ms polling start */
	bspDinApplyDebounce();  /* set debounce for mechanical switches */

	ledInit();          /* NUCLEO on-board LEDs */
	bspAinInit();
	bspAoutInit();
	bspPwmInit();
}

/*
 * Apply debounce to all mechanical input channels.
 * Switches and relay feedback contacts need ~10ms debounce;
 * digital status inputs (already clean) use 0.
 */
static void bspDinApplyDebounce(void)
{
	static const bspDinSettings deb_10ms = { .deb_en = true, .deb_time = 10 };
	static const bspDinSettings deb_off  = { .deb_en = false, .deb_time = 0 };

	/* Mechanical switches — 10ms debounce */
	bspDinSetDebouncing(DIN_SYSTEM_ON_OFF,          deb_10ms);
	bspDinSetDebouncing(DIN_SYSTEM_RESET,           deb_10ms);
	bspDinSetDebouncing(DIN_S1_SYSTEM_CONFIG,       deb_10ms);
	bspDinSetDebouncing(DIN_S2_SYSTEM_CONFIG,       deb_10ms);
	bspDinSetDebouncing(DIN_SOLO_SYSTEM_CONFIG,     deb_10ms);
	bspDinSetDebouncing(DIN_TROLLEY_CONNECTED,      deb_10ms);
	bspDinSetDebouncing(DIN_TROLLEY_CONNECTED_J,    deb_10ms);
	bspDinSetDebouncing(DIN_SMART_WHS_INDICATE,     deb_10ms);
	bspDinSetDebouncing(DIN_DRAWER_INDICATE,        deb_10ms);
	bspDinSetDebouncing(DIN_SMART_CTRL_WHS_SEARCH,  deb_10ms);

	/* Status / LED monitor inputs — no debounce needed */
	bspDinSetDebouncing(DIN_GRID_MAIN_RELAY_STATUS, deb_off);
	bspDinSetDebouncing(DIN_ME_BOX_ERROR,           deb_off);
	bspDinSetDebouncing(DIN_TEMP_ALERT,             deb_off);
	bspDinSetDebouncing(DIN_LED_PWR_24_ON,          deb_off);
	bspDinSetDebouncing(DIN_LED_CP_24V_ON,          deb_off);
	bspDinSetDebouncing(DIN_IS_PC_ON,               deb_off);
	bspDinSetDebouncing(DIN_APP_HOST_ON,            deb_off);
}

/*----------------------------------------------------------------------------*/
/*! @brief I2C transfer (write then read) */
/*----------------------------------------------------------------------------*/
bool boardI2CTransfer(uint8_t i2cNum, uint16_t devAddr,
                      uint8_t *wrBuf, uint16_t wrSize,
                      uint8_t *rdBuf, uint16_t rdSize)
{
    /* Map legacy "i2cNum" to Zephyr I2C device(s) */
    const struct device *i2c = NULL;

    if (i2cNum == 1) {
        i2c = i2c0_dev;
    } else {
        return false;
    }

    if (!i2c || !device_is_ready(i2c)) {
        return false;
    }

    /* STM32 HAL often used 8-bit address <<1; Zephyr expects 7-bit address. */
    const uint16_t addr7 = (devAddr > 0x7F) ? (devAddr >> 1) : devAddr;

    struct i2c_msg msgs[2];
    int msg_count = 0;

    if (wrBuf && wrSize) {
        msgs[msg_count].buf   = wrBuf;
        msgs[msg_count].len   = wrSize;
        msgs[msg_count].flags = I2C_MSG_WRITE;
        msg_count++;
    }

    if (rdBuf && rdSize) {
        msgs[msg_count].buf   = rdBuf;
        msgs[msg_count].len   = rdSize;
        msgs[msg_count].flags = I2C_MSG_READ | I2C_MSG_STOP;
        msg_count++;
    } else if (msg_count > 0) {
        /* If only write, ensure STOP */
        msgs[msg_count - 1].flags |= I2C_MSG_STOP;
    }

    if (msg_count == 0) {
        return true;
    }

    return (i2c_transfer(i2c, msgs, msg_count, addr7) == 0);
}

/*----------------------------------------------------------------------------*/
/*! @brief SPI transfer (full duplex) */
/*----------------------------------------------------------------------------*/
bool boardSpiTransfer(boardSpiXfer *xfer)
{
    if (!xfer) {
        return false;
    }

    /* Minimal mapping: only spi0 supported here */
    const struct device *spi = spi0_dev;
    if (!spi || !device_is_ready(spi)) {
        return false;
    }

    /* You need to define CS pins in devicetree and/or provide them here.
     * For now we assume CS is controlled externally or by the controller node.
     * If you require per-device CS, add gpio_dt_spec per device and fill spi_cs_control.
     */
    struct spi_config cfg = {
        .frequency = 1000000U, /* TODO: make configurable */
        .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
        .slave     = 0,
    };

    const struct spi_buf tx_buf = {
        .buf = xfer->wrBuf,
        .len = xfer->size,
    };
    const struct spi_buf rx_buf = {
        .buf = xfer->rdBuf,
        .len = xfer->size,
    };
    const struct spi_buf_set tx = {
        .buffers = &tx_buf,
        .count   = xfer->wrBuf ? 1U : 0U,
    };
    const struct spi_buf_set rx = {
        .buffers = &rx_buf,
        .count   = xfer->rdBuf ? 1U : 0U,
    };

    /* If one direction is NULL, use spi_read/spi_write accordingly */
    int ret;
    if (xfer->wrBuf && xfer->rdBuf) {
        ret = spi_transceive(spi, &cfg, &tx, &rx);
    } else if (xfer->wrBuf) {
        ret = spi_write(spi, &cfg, &tx);
    } else if (xfer->rdBuf) {
        ret = spi_read(spi, &cfg, &rx);
    } else {
        return true;
    }

    return (ret == 0);
}