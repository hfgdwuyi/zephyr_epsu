/*!
 * Copyright © Siemens Healthcare GmbH 2022, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 * @file
 * @brief Header file for bsp_led.c
 */
/*----------------------------------------------------------------------------*/
#ifndef BSP_LED_H
#define BSP_LED_H

/* Standard library */
#include <stdint.h>

/*! Board tri-color LEDs (PC8/9/10):
 * 0 = green (PC10, OK), 1 = red (PC9, FAULT), 2 = yellow (PC8, WARN) */
#define SYSTEM_OK_LED_NUM    (0)   /* green  PC10 */
#define SYSTEM_FAULT_LED_NUM (1)   /* red    PC9  */
#define SYSTEM_WARN_LED_NUM  (2)   /* yellow PC8  */

/* Legacy aliases */
#define SYSTEM_ERROR_LED_NUM SYSTEM_FAULT_LED_NUM
#define CAN_ERROR_LED_NUM    SYSTEM_FAULT_LED_NUM
#define CAN_STATUS_LED_NUM   SYSTEM_OK_LED_NUM

void bspLedInit(void);
void bspLedSwitchOn(uint8_t ledNumber);
void bspLedSwitchOff(uint8_t ledNumber);
void bspLedToggle(uint8_t ledNumber);

#endif

//--------------------------------- End Of File -------------------------------/
