/*!
 * Copyright © Siemens Healthcare GmbH 2023, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 * @file
 * @brief Pins and interfaces initialization (Zephyr port)
 */
/*----------------------------------------------------------------------------*/

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/can.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bsp_led.h"
#include "bsp_board.h"
#include "bsp_ain.h"
#include "bsp_aout.h"
#include "bsp_pwm.h"

#ifndef BOOTLOADER
#include "message_queue.h"
#endif

/*----------------------------------------------------------------------------
 * Devicetree bindings
 *----------------------------------------------------------------------------
 * board DTS / overlay should provide:
 * - chosen { zephyr,console = &<uart>; }  (optional, for printk)
 * - aliases { serial0 = &<uart>; }       (optional)
 *
 * For this BSP we pick:
 * - UART: DT_CHOSEN(zephyr_console) if okay; else DT_ALIAS(serial0)
 * - I2C:  DT_ALIAS(i2c0) (or change to your bus)
 * - SPI:  DT_ALIAS(spi0) and DT_ALIAS(spi1) optional
 *
 * You may need to adjust the alias names to match your board DTS.
 */

#if DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_console), okay)
#define BSP_UART_NODE DT_CHOSEN(zephyr_console)
#elif DT_NODE_HAS_STATUS(DT_ALIAS(serial0), okay)
#define BSP_UART_NODE DT_ALIAS(serial0)
#else
#error "No UART chosen: set zephyr,console or alias serial0 in devicetree"
#endif


static const struct device *const serial_dev = DEVICE_DT_GET_OR_NULL(BSP_UART_NODE);

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



#define BOARD_SERIAL_BUFFER_SIZE 512

#ifndef BOOTLOADER
static uint8_t boardSerialRxBuffer[BOARD_SERIAL_BUFFER_SIZE];
static messageQueue boardMqSerialRx = {
    .elSize = sizeof(uint8_t),
    .elNum  = BOARD_SERIAL_BUFFER_SIZE,
    .data   = boardSerialRxBuffer,
};
#endif

/*----------------------------------------------------------------------------*/
/*! @brief Pins / peripherals initialization (Zephyr) */
/*----------------------------------------------------------------------------*/
void boardInit(void)
{
    /* In Zephyr, pinmux and peripheral clocks are configured by devicetree + drivers.
     * This function typically just validates device readiness and initializes BSP modules.
     */
    if (!device_is_ready(serial_dev)) {
        printk("boardInit: UART device not ready\n");
    }

    if (i2c0_dev && !device_is_ready(i2c0_dev)) {
        printk("boardInit: I2C0 device not ready\n");
    }

    if (spi0_dev && !device_is_ready(spi0_dev)) {
        printk("boardInit: SPI0 device not ready\n");
    }

    /* LED module init */
    ledInit();

    bspAinInit();

    bspAoutInit();

    bspAoutInit();

    bspPwmInit();

}

/*----------------------------------------------------------------------------*/
/*! @brief Output one character to serial (polling) */
/*----------------------------------------------------------------------------*/
int32_t boardSerialSend(char c)
{
    if (!device_is_ready(serial_dev)) {
        return EOF;
    }

    uart_poll_out(serial_dev, (unsigned char)c);
    return (int32_t)c;
}

/*----------------------------------------------------------------------------*/
/*! @brief Receive character from serial (non-blocking, polling) */
/*----------------------------------------------------------------------------*/
#ifndef BOOTLOADER
int32_t boardSerialReceive(void)
{
    /* Keep legacy message queue interface. This requires that someone pushes bytes
     * into boardMqSerialRx. In this minimal port we do a poll-in and push here.
     *
     * If you later switch to UART interrupt-driven RX, move pushing into ISR callback.
     */
    if (!device_is_ready(serial_dev)) {
        return EOF;
    }

    uint8_t ch;
    while (uart_poll_in(serial_dev, &ch) == 0) {
        (void)msgQueuePush(&boardMqSerialRx, &ch);
    }

    uint8_t tmp;
    if (msgQueuePop(&boardMqSerialRx, &tmp) == MSGQ_OK) {
        return (int32_t)tmp;
    }
    return EOF;
}
#else
int32_t boardSerialReceive(char *buf)
{
    if (!buf) {
        return 0;
    }
    buf[0] = (char)EOF;

    if (!device_is_ready(serial_dev)) {
        return 0;
    }

    uint8_t ch;
    if (uart_poll_in(serial_dev, &ch) == 0) {
        buf[0] = (char)ch;
        return 1;
    }

    return 0;
}
#endif

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