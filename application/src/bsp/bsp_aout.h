/*!
 * Copyright (c) Siemens Healthcare GmbH 2022, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 * @file
 * @brief Header file for bsp_aout.c (Zephyr port) — BSP 层只提供 DAC 原语。
 *        状态指示灯逻辑（呼吸/常亮/灭）在应用层 indicator 模块。
 */
/*----------------------------------------------------------------------------*/
#ifndef BSP_AOUT_H
#define BSP_AOUT_H

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
