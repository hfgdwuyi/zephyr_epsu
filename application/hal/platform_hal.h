/*
 * platform_hal.h — 平台抽象层总头文件
 *
 * 业务代码只include这一个头文件，不直接接触物理管脚或RTOS API。
 */

#ifndef PLATFORM_HAL_H
#define PLATFORM_HAL_H

#include "hal_gpio.h"
#include "hal_adc.h"
#include "hal_pwm.h"
#include "hal_wdt.h"
#include "hal_board.h"
#include "hal_can.h"

#endif /* PLATFORM_HAL_H */
