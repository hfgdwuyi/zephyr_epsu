/*!
 * Copyright Siemens Healthcare GmbH 2022, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 * @file
 * @brief Header file for bsp_pwm.c (Zephyr port)
 */
/*----------------------------------------------------------------------------*/
#ifndef BSP_PWM_H
#define BSP_PWM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void bspPwmInit(void);
void bspPwmStart(uint8_t pwmNum);
void bspPwmStop(uint8_t pwmNum);

/* frequency in Hz */
void bspPwmSetCarrierFreq(uint8_t pwmNum, uint32_t frequency);

/* duty cycle in percent [0..100] */
void bspPwmSetDutyCycle(uint8_t pwmNum, uint32_t value);

/* Current duty cycle (%) — status query for host tools */
uint32_t bspPwmGetDutyCycle(uint8_t pwmNum);

#ifdef __cplusplus
}
#endif

#endif /* BSP_PWM_H */

/*--------------------------------- End Of File -------------------------------*/
