/*!
 * Copyright (c) Siemens Healthcare GmbH 2022, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 * @file
 * @brief Header file for bsp_aout.c (Zephyr port)
 */
/*----------------------------------------------------------------------------*/
#ifndef BSP_AOUT_H
#define BSP_AOUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void bspAoutInit(void);

/* writeValue is interpreted as millivolts (mV) in the current Zephyr port */
void bspAoutWrite(uint8_t channel, int16_t writeValue);

#ifdef __cplusplus
}
#endif

#endif /* BSP_AOUT_H */