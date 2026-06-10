/*!
 * Copyright © Siemens Healthcare GmbH 2022, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 * @file
 * @brief    Digital input/output pin control functions (Zephyr port)
 */
/*----------------------------------------------------------------------------*/

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>

#include "bsp_dio.h"
#include "timing.h"

/*
 * This file was originally HAL-based (HAL_GPIO_*, GPIO_InitTypeDef, etc).
 * Ported to Zephyr GPIO API.
 *
 * IMPORTANT:
 * - gpio_dt_spec.dt_flags is meant for devicetree GPIO flags (uint16_t).
 * - Do NOT store runtime configuration flags like GPIO_OUTPUT_INACTIVE there.
 *   That caused overflow warnings.
 *
 * Therefore we use a small runtime spec (port/pin/flags) and configure pins via
 * gpio_pin_configure().
 */

/* Map STM32 GPIO port letter to Zephyr gpio device node */
#define GPIO_DEV_A DEVICE_DT_GET(DT_NODELABEL(gpioa))
#define GPIO_DEV_B DEVICE_DT_GET(DT_NODELABEL(gpiob))
#define GPIO_DEV_C DEVICE_DT_GET(DT_NODELABEL(gpioc))
#define GPIO_DEV_D DEVICE_DT_GET(DT_NODELABEL(gpiod))
#define GPIO_DEV_E DEVICE_DT_GET(DT_NODELABEL(gpioe))
#define GPIO_DEV_F DEVICE_DT_GET(DT_NODELABEL(gpiof))
#define GPIO_DEV_G DEVICE_DT_GET(DT_NODELABEL(gpiog))
#define GPIO_DEV_H DEVICE_DT_GET(DT_NODELABEL(gpioh))
#define GPIO_DEV_I DEVICE_DT_GET(DT_NODELABEL(gpioi))
#define GPIO_DEV_J DEVICE_DT_GET(DT_NODELABEL(gpioj))
#define GPIO_DEV_K DEVICE_DT_GET(DT_NODELABEL(gpiok))

/* Runtime gpio spec (not devicetree gpios spec) */
struct bsp_gpio_spec {
    const struct device *port;
    gpio_pin_t pin;
    gpio_flags_t flags;
};

#define BSP_GPIO_SPEC(_dev, _pin, _flags) \
    ((struct bsp_gpio_spec){ .port = (_dev), .pin = (gpio_pin_t)(_pin), .flags = (gpio_flags_t)(_flags) })

/* Translate old HAL intent to Zephyr flags (simplified):
 * - Outputs were OUTPUT_PP + PULLUP => output, with pull-up.
 * - Inputs were INPUT + PULLUP => input, with pull-up.
 * Speed doesn't exist in Zephyr GPIO API and is ignored here.
 */
#define BSP_DOUT_FLAGS (GPIO_OUTPUT_INACTIVE | GPIO_PULL_UP)
#define BSP_DIN_FLAGS  (GPIO_INPUT | GPIO_PULL_UP)

/* If your inputs/outputs are active-low in hardware, you can OR in GPIO_ACTIVE_LOW. */

/* Static functions declaration */
static void bspDoutInit(void);
static void bspDinInit(void);
static void bspDinUpdate(void);

/*----------------------------------------------------------------------------*/
/*! @name IO configuration @{ */
/*----------------------------------------------------------------------------*/

/* Output pin table (ported).
 * Original comments kept.
 */
static const struct bsp_gpio_spec pinCfgDout[] = {
    /* Byte 0 CANOpen subindex 1 */
    BSP_GPIO_SPEC(GPIO_DEV_F, 2,  BSP_DOUT_FLAGS), /* TACT-FD1-CONT-TEST */
    BSP_GPIO_SPEC(GPIO_DEV_F, 3,  BSP_DOUT_FLAGS), /* TACT-FD2-CONT-TEST */
    BSP_GPIO_SPEC(GPIO_DEV_F, 4,  BSP_DOUT_FLAGS), /* TACT-MB1-CONT-TEST */
    BSP_GPIO_SPEC(GPIO_DEV_F, 5,  BSP_DOUT_FLAGS), /* TACT-MB2-CONT-TEST */
    BSP_GPIO_SPEC(GPIO_DEV_F, 1,  BSP_DOUT_FLAGS), /* TACT-CARM-CONT-TEST */
    BSP_GPIO_SPEC(GPIO_DEV_F, 0,  BSP_DOUT_FLAGS), /* TACT-CBRK-TEST */
    BSP_GPIO_SPEC(GPIO_DEV_A, 11, BSP_DOUT_FLAGS), /* EMCY-TEST1 */
    BSP_GPIO_SPEC(GPIO_DEV_A, 12, BSP_DOUT_FLAGS), /* EMCY-TEST2 */

    /* Byte 1 CANOpen subindex 2 (schematic page 29, MCU2: PI/PJ output group) */
    BSP_GPIO_SPEC(GPIO_DEV_I, 0,  BSP_DOUT_FLAGS), /* UI-OUT(1): K5_DRV  */
    BSP_GPIO_SPEC(GPIO_DEV_I, 3,  BSP_DOUT_FLAGS), /* UI-OUT(2): K6_DRV  */
    BSP_GPIO_SPEC(GPIO_DEV_I, 7,  BSP_DOUT_FLAGS), /* UI-OUT(3): K8_1_DRV */
    BSP_GPIO_SPEC(GPIO_DEV_I, 8,  BSP_DOUT_FLAGS), /* UI-OUT(4): K8_2_DRV */
    BSP_GPIO_SPEC(GPIO_DEV_I, 12, BSP_DOUT_FLAGS), /* UI-OUT(5): K9_DRV  */
    BSP_GPIO_SPEC(GPIO_DEV_I, 15, BSP_DOUT_FLAGS), /* UI-OUT(6): K10_DRV */
    BSP_GPIO_SPEC(GPIO_DEV_J, 4,  BSP_DOUT_FLAGS), /* UI-OUT(7): K11_DRV */
    BSP_GPIO_SPEC(GPIO_DEV_J, 11, BSP_DOUT_FLAGS), /* UI-OUT(8): K12_DRV */

    /* Byte 2 CANOpen subindex 3 */
    BSP_GPIO_SPEC(GPIO_DEV_C, 7,  BSP_DOUT_FLAGS), /* LASER-FD-PWR-EN */
    BSP_GPIO_SPEC(GPIO_DEV_C, 9,  BSP_DOUT_FLAGS), /* LASER-MB-PWR-EN */
    BSP_GPIO_SPEC(GPIO_DEV_J, 6,  BSP_DOUT_FLAGS), /* PUMP-EN */
    BSP_GPIO_SPEC(GPIO_DEV_G, 2,  BSP_DOUT_FLAGS), /* FORCE-PWR-EN */
    BSP_GPIO_SPEC(GPIO_DEV_D, 1,  BSP_DOUT_FLAGS), /* GRIP-PWR-EN */
    BSP_GPIO_SPEC(GPIO_DEV_H, 10, BSP_DOUT_FLAGS), /* EMCY_LED */
    BSP_GPIO_SPEC(GPIO_DEV_G, 0,  BSP_DOUT_FLAGS), /* TLC5917_OE_N */
    BSP_GPIO_SPEC(GPIO_DEV_G, 7,  BSP_DOUT_FLAGS), /* UI-PWR-EN */

    /* Byte 3 CANOpen subindex 4 */
    BSP_GPIO_SPEC(GPIO_DEV_G, 3,  BSP_DOUT_FLAGS), /* CAN-PWR-EN */
    BSP_GPIO_SPEC(GPIO_DEV_G, 5,  BSP_DOUT_FLAGS), /* COLLI-PWR-EN */
    BSP_GPIO_SPEC(GPIO_DEV_C, 12, BSP_DOUT_FLAGS), /* GRID-PWR-EN */
};

/* Input pin table (ported). */
static const struct bsp_gpio_spec pinCfgDin[] = {
    /* Byte 0 CANOpen subindex 1 */
    BSP_GPIO_SPEC(GPIO_DEV_G, 11, BSP_DIN_FLAGS), /* CAP-SENS1 */
    BSP_GPIO_SPEC(GPIO_DEV_G, 12, BSP_DIN_FLAGS), /* CAP-SENS2 */
    BSP_GPIO_SPEC(GPIO_DEV_E, 13, BSP_DIN_FLAGS), /* TACT-CARM-CBRK-C */
    BSP_GPIO_SPEC(GPIO_DEV_E, 11, BSP_DIN_FLAGS), /* TACT-FD1-CBRK-C */
    BSP_GPIO_SPEC(GPIO_DEV_F, 14, BSP_DIN_FLAGS), /* TACT-FD2-CBRK-C */
    BSP_GPIO_SPEC(GPIO_DEV_E, 12, BSP_DIN_FLAGS), /* TACT-MB1-CBRK-C */
    BSP_GPIO_SPEC(GPIO_DEV_F, 15, BSP_DIN_FLAGS), /* TACT-MB2-CBRK-C */
    BSP_GPIO_SPEC(GPIO_DEV_E, 10, BSP_DIN_FLAGS), /* TACT-CARM-CONT-C */

    /* Byte 1 CANOpen subindex 2 */
    BSP_GPIO_SPEC(GPIO_DEV_E, 8,  BSP_DIN_FLAGS), /* TACT-FD1-CONT-C */
    BSP_GPIO_SPEC(GPIO_DEV_F, 12, BSP_DIN_FLAGS), /* TACT-FD2_CONT-C */
    BSP_GPIO_SPEC(GPIO_DEV_E, 9,  BSP_DIN_FLAGS), /* TACT-MB1-CONT-C */
    BSP_GPIO_SPEC(GPIO_DEV_F, 13, BSP_DIN_FLAGS), /* TACT-MB2-CONT-C */
    BSP_GPIO_SPEC(GPIO_DEV_E, 14, BSP_DIN_FLAGS), /* EMCY-IN1-C */
    BSP_GPIO_SPEC(GPIO_DEV_E, 15, BSP_DIN_FLAGS), /* EMCY-IN2-C */
    BSP_GPIO_SPEC(GPIO_DEV_E, 7,  BSP_DIN_FLAGS), /* P24V0-SI-PWRGD */
    BSP_GPIO_SPEC(GPIO_DEV_E, 0,  BSP_DIN_FLAGS), /* TACT-CBRK-OR */

    /* Byte 2 CANOpen subindex 3 */
    BSP_GPIO_SPEC(GPIO_DEV_G, 4,  BSP_DIN_FLAGS), /* CAN-PWR-FLT */
    BSP_GPIO_SPEC(GPIO_DEV_G, 6,  BSP_DIN_FLAGS), /* COLLI-PWR-FLT */
    BSP_GPIO_SPEC(GPIO_DEV_C, 13, BSP_DIN_FLAGS), /* GRID-PWR-FLT */
    BSP_GPIO_SPEC(GPIO_DEV_C, 8,  BSP_DIN_FLAGS), /* LASER-FD-FLT */
    BSP_GPIO_SPEC(GPIO_DEV_C, 10, BSP_DIN_FLAGS), /* LASER-MB-FLT */
    BSP_GPIO_SPEC(GPIO_DEV_G, 1,  BSP_DIN_FLAGS), /* FORCE-PWR-FLT */
    BSP_GPIO_SPEC(GPIO_DEV_D, 0,  BSP_DIN_FLAGS), /* GRIP-PWR-FLT */
    BSP_GPIO_SPEC(GPIO_DEV_G, 13, BSP_DIN_FLAGS), /* UI_PWR_FLT */

    /* Byte 3 CANOpen subindex 4 */
    BSP_GPIO_SPEC(GPIO_DEV_I, 6,  BSP_DIN_FLAGS), /* UI-IN(1) */
    BSP_GPIO_SPEC(GPIO_DEV_I, 7,  BSP_DIN_FLAGS), /* UI-IN(2) */
    BSP_GPIO_SPEC(GPIO_DEV_I, 8,  BSP_DIN_FLAGS), /* UI-IN(3) */
    BSP_GPIO_SPEC(GPIO_DEV_I, 9,  BSP_DIN_FLAGS), /* UI-IN(4) */
    BSP_GPIO_SPEC(GPIO_DEV_I, 10, BSP_DIN_FLAGS), /* UI-IN(5) */
    BSP_GPIO_SPEC(GPIO_DEV_I, 11, BSP_DIN_FLAGS), /* UI-IN(6) */
    BSP_GPIO_SPEC(GPIO_DEV_I, 12, BSP_DIN_FLAGS), /* UI-IN(7) */
    BSP_GPIO_SPEC(GPIO_DEV_I, 13, BSP_DIN_FLAGS), /* UI-IN(8) */

    /* Byte 4 CANOpen subindex 5 */
    BSP_GPIO_SPEC(GPIO_DEV_G, 9,  BSP_DIN_FLAGS), /* INERTIAL_INT1 */
    BSP_GPIO_SPEC(GPIO_DEV_G, 10, BSP_DIN_FLAGS), /* INERTIAL_INT2 */
    BSP_GPIO_SPEC(GPIO_DEV_B, 12, BSP_DIN_FLAGS), /* TEMP_ALERT */
    BSP_GPIO_SPEC(GPIO_DEV_C, 11, BSP_DIN_FLAGS), /* GRID-STATUS */
    BSP_GPIO_SPEC(GPIO_DEV_E, 13, BSP_DIN_FLAGS), /* UI-P24V-DET */
};

/*! @} */

/* Number of DINs/DOUTs (computed from config tables) */
#define DIN_MAX  (ARRAY_SIZE(pinCfgDin))
#define DOUT_MAX (ARRAY_SIZE(pinCfgDout))

/* Number of DIN/DOUT bytes */
#define DIN_BYTES  ((DIN_MAX + CHAR_BIT - 1) / CHAR_BIT)
#define DOUT_BYTES ((DOUT_MAX + CHAR_BIT - 1) / CHAR_BIT)

/*! Number of Digital input pins */
const uint8_t dinMax = DIN_MAX;
/*! Current state of digital inputs */
static uint8_t dinState[DIN_BYTES];
/*! Digital inputs debouncing settings */
static bspDinSettings dinSettings[DIN_MAX];

/*! Number of Digital output pins */
const uint8_t doutMax = DOUT_MAX;

/*----------------------------------------------------------------------------*/
/*! @brief Init digital input/output pins */
/*----------------------------------------------------------------------------*/
void bspDioInit(void)
{
    bspDinInit();
    bspDoutInit();
}

/*----------------------------------------------------------------------------*/
/*! @brief Digital input pins initialization */
/*----------------------------------------------------------------------------*/
static void bspDinInit(void)
{
    static timingTimer dinTimer;

    for (uint8_t i = 0; i < dinMax; i++) {
        if (!device_is_ready(pinCfgDin[i].port)) {
            /* If a GPIO controller isn't ready, we just skip configuring that pin. */
            continue;
        }
        (void)gpio_pin_configure(pinCfgDin[i].port, pinCfgDin[i].pin, pinCfgDin[i].flags);
    }

    /* Start periodic state read (1 ms as in original code) */
    (void)timingAddTimer(&dinTimer, TIMING_TIMER_CYCLIC, 1, bspDinUpdate);
}

/*----------------------------------------------------------------------------*/
/*! @brief Set up DIN debouncing settings */
/*----------------------------------------------------------------------------*/
void bspDinSetDebouncing(uint8_t pin, bspDinSettings settings)
{
    if (pin < DIN_MAX) {
        dinSettings[pin] = settings;
    }
}

/*----------------------------------------------------------------------------*/
/*! @brief Handler routine for periodic pin state read */
/*----------------------------------------------------------------------------*/
static void bspDinUpdate(void)
{
    static uint32_t debounceTime[DIN_MAX] = { 0 };

    for (uint8_t dinIndex = 0; dinIndex < dinMax; dinIndex++) {
        const uint8_t bit  = dinIndex % CHAR_BIT;
        const uint8_t byte = dinIndex / CHAR_BIT;

        if (!device_is_ready(pinCfgDin[dinIndex].port)) {
            continue;
        }

        const int v = gpio_pin_get(pinCfgDin[dinIndex].port, pinCfgDin[dinIndex].pin);
        const bool curState = (v > 0);

        const bool oldValue = ((dinState[byte] & BIT(bit)) != 0U);

        const uint8_t debTime = dinSettings[dinIndex].deb_en ? dinSettings[dinIndex].deb_time : 0U;

        /* Debouncing (kept same logic) */
        if (oldValue != curState) {
            debounceTime[dinIndex]++;
            if (debounceTime[dinIndex] >= debTime) {
                if (curState) {
                    dinState[byte] |= BIT(bit);
                } else {
                    dinState[byte] &= (uint8_t)~BIT(bit);
                }
                debounceTime[dinIndex] = 0U;
            }
        } else {
            debounceTime[dinIndex] = 0U;
        }
    }
}

/*----------------------------------------------------------------------------*/
/*! @brief Read a byte with DIN states */
/*----------------------------------------------------------------------------*/
uint8_t bspDinRead(uint8_t byte)
{
    if (byte >= DIN_BYTES) {
        return 0U;
    }
    return dinState[byte];
}

/*----------------------------------------------------------------------------*/
/*! @brief Digital output pins initialization */
/*----------------------------------------------------------------------------*/
static void bspDoutInit(void)
{
    for (uint8_t i = 0; i < doutMax; i++) {
        if (!device_is_ready(pinCfgDout[i].port)) {
            continue;
        }
        (void)gpio_pin_configure(pinCfgDout[i].port, pinCfgDout[i].pin, pinCfgDout[i].flags);
        (void)gpio_pin_set(pinCfgDout[i].port, pinCfgDout[i].pin, 0);
    }
}

/*----------------------------------------------------------------------------*/
/*! @brief Set specific output pin state */
/*----------------------------------------------------------------------------*/
void bspDoutSet(uint8_t pinNumber, bool state)
{
    if (pinNumber >= doutMax) {
        return;
    }

    if (!device_is_ready(pinCfgDout[pinNumber].port)) {
        return;
    }

    (void)gpio_pin_set(pinCfgDout[pinNumber].port, pinCfgDout[pinNumber].pin, state ? 1 : 0);
}

/*----------------------------------------------------------------------------*/
/*! @brief Read output pin state */
/*----------------------------------------------------------------------------*/
bool bspDoutRead(uint8_t pinNumber)
{
    if (pinNumber >= doutMax) {
        return false;
    }
    if (!device_is_ready(pinCfgDout[pinNumber].port)) {
        return false;
    }

    const int v = gpio_pin_get(pinCfgDout[pinNumber].port, pinCfgDout[pinNumber].pin);
    return (v > 0);
}

/*
 * NOTE ABOUT terminal/constructor:
 * The original file registers terminal commands via __attribute__((constructor))
 * and uses a custom "terminal.h". This is not Zephyr shell.
 * For a clean Zephyr port, integrate with Zephyr shell (CONFIG_SHELL) separately.
 * We intentionally removed/disabled the legacy terminal integration here.
 */