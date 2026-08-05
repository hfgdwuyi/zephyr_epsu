/*!
 * Copyright Siemens Healthcare GmbH 2022, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 * @file
 * @brief Pins and interfaces initialization (Zephyr port)
 */
/*----------------------------------------------------------------------------*/
#ifndef BSP_BOARD_H
#define BSP_BOARD_H

#include <stdbool.h>
#include <stdint.h>

/* Keep TLC driver integration as-is (your project header) */
// #include "tlc591x.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t  spi;      /* Legacy SPI bus index (implementation maps this to a DT alias) */
    void    *wrBuf;
    void    *rdBuf;
    uint16_t size;
    uint32_t timeout;  /* In Zephyr port this is currently ignored (non-blocking APIs use driver defaults) */
} boardSpiXfer_t;

#define SYS_I2C_TMP_ADDRESS 0x48U    /* Temperature sensor's 7-bit address */

// extern tlc591xInstance tlcLedDriver;

void    bspBoardInit(void);

/* I2C transfer: write (optional) then read (optional). devAddr can be 7-bit or 8-bit (HAL style). */
bool bspBoardI2CTransfer(uint8_t i2cNum, uint16_t devAddr,
                      uint8_t *wrBuf, uint16_t wrSize,
                      uint8_t *rdBuf, uint16_t rdSize);

/* SPI transfer: uses Zephyr SPI API; xfer->spi selects which SPI controller is used */
bool bspBoardSpiTransfer(boardSpiXfer_t *xfer);

/* WDT init: legacy API. In Zephyr port it should call bspWtdgInit() */
void bspWtdgInit(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BOARD_H */