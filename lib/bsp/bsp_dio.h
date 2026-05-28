/*
 * Copyright © Siemens Healthcare GmbH 2022, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 * @file
 * @brief    Header file for bsp_dio.c (Zephyr port)
 */
/*----------------------------------------------------------------------------*/
#ifndef BSP_DIO_H
#define BSP_DIO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool    deb_en;    /* Debouncing enabled */
    uint8_t deb_time;  /* Debouncing time (in update ticks; current implementation uses 1ms tick) */
} bspDinSettings;

extern const uint8_t dinMax;
extern const uint8_t doutMax;

void bspDioInit(void);

/* Configure debouncing for a DIN pin index [0..dinMax-1] */
void bspDinSetDebouncing(uint8_t pin, bspDinSettings settings);

/* Read one packed DIN byte (byte index [0..DIN_BYTES-1] as implemented in bsp_dio.c) */
uint8_t bspDinRead(uint8_t byte);

/* Set/read DOUT by pin index [0..doutMax-1] */
void bspDoutSet(uint8_t pinNumber, bool state);
bool bspDoutRead(uint8_t pinNumber);

#ifdef __cplusplus
}
#endif

#endif /* BSP_DIO_H */
/*-------------------------------- End Of File -------------------------------*/