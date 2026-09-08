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

/*! 产品板 PC8/9/10 三色灯编号：
 * 0 = 绿(PC10, OK/正常)、1 = 红(PC9, FAULT/故障)、2 = 黄(PC8, WARN/告警) */
#define SYSTEM_OK_LED_NUM    (0)   /* 绿 PC10 */
#define SYSTEM_FAULT_LED_NUM (1)   /* 红 PC9  */
#define SYSTEM_WARN_LED_NUM  (2)   /* 黄 PC8  */

/* 兼容旧名 */
#define SYSTEM_ERROR_LED_NUM SYSTEM_FAULT_LED_NUM
#define CAN_ERROR_LED_NUM    SYSTEM_FAULT_LED_NUM
#define CAN_STATUS_LED_NUM   SYSTEM_OK_LED_NUM

void bspLedInit(void);
void bspLedSwitchOn(uint8_t ledNumber);
void bspLedSwitchOff(uint8_t ledNumber);
void bspLedToggle(uint8_t ledNumber);

#endif

//--------------------------------- End Of File -------------------------------/
