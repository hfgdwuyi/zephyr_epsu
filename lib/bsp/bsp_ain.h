/*!
 * Copyright (c) Siemens Healthcare GmbH 2022, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 * @file
 * @brief Header file for bsp_ain.c (Zephyr port)
 */
/*----------------------------------------------------------------------------*/
#ifndef BSP_AIN_H
#define BSP_AIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void     bspAinInit(void);

/* Polling API: call periodically from main loop to refresh latest ADC values */
void     bspAinPoll(void);

uint32_t bspAinGetRawValue(uint8_t channel);

/* Optional helper (currently returns "AIN0" etc. depending on implementation) */
const char *bspAinGetName(uint8_t channel);

/* Number of configured analog input channels (from devicetree io-channels list) */
extern uint8_t AIN_NUMBER;

#ifdef __cplusplus
}
#endif

#endif /* BSP_AIN_H */