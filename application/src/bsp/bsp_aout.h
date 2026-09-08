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

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AOUT channels */
enum {
	AOUT_PWR_ON_OFF = 0,   /* DAC1_OUT2 / PA5 — 0-1.5 V @ 0.25 Hz */
	AOUT_CH_COUNT,
};

void bspAoutInit(void);

/* Immediate write; value is in millivolts (mV) */
void bspAoutWrite(uint8_t channel, int16_t writeValue);

/* Last written DAC voltage (mV) — status query for host tools */
int16_t bspAoutGetMv(uint8_t channel);

/* State-bit driven control (like DOUT): the state machine sets whether
 * a channel is active via bspAoutSetState(); bspAoutPoll() (called from
 * the scheduler) drives the DAC accordingly each period. */
void bspAoutSetState(uint8_t channel, bool active);
bool bspAoutGetState(uint8_t channel);
void bspAoutPoll(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_AOUT_H */