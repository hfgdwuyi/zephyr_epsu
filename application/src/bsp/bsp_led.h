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

#include <stdint.h>

/*! System ERROR LED number */
#define SYSTEM_ERROR_LED_NUM (0)
/*! System OK LED number */
#define SYSTEM_OK_LED_NUM (1)
/*! CAN error LED number */
#define CAN_ERROR_LED_NUM (2)
/*! CAN status LED number */
#define CAN_STATUS_LED_NUM (3)

void bspLedInit(void);
void bspLedSwitchOn(uint8_t ledNumber);
void bspLedSwitchOff(uint8_t ledNumber);
void bspLedToggle(uint8_t ledNumber);

#endif

//--------------------------------- End Of File -------------------------------/
