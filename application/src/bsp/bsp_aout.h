/*!
 * Copyright (c) Siemens Healthcare GmbH 2022, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 * @file
 * @brief Header for bsp_aout.c - the BSP only provides DAC primitives.
 *        Indicator logic lives in the application indicator module.
 */
/*----------------------------------------------------------------------------*/
#ifndef BSP_AOUT_H
#define BSP_AOUT_H

/* Standard library */
#include <stdbool.h>
#include <stdint.h>

/* AOUT channels */
enum {
	AOUT_PWR_ON_OFF = 0,   /* DAC1_OUT2 / PA5 */
	AOUT_CH_COUNT,
};

void bspAoutInit(void);

/* Immediate write; value is in millivolts (mV) */
void bspAoutWrite(uint8_t channel, int16_t writeValue);

/* Last written DAC voltage (mV) — status query for host tools */
int16_t bspAoutGetMv(uint8_t channel);

#endif /* BSP_AOUT_H */
