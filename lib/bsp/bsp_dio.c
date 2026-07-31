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
/*! @name IO configuration (cios-zhong pin_config.xlsx) @{ */
/*----------------------------------------------------------------------------*/

/* Digital Output pin table */
static const struct bsp_gpio_spec pinCfgDout[] = {
    /* Relay & power drivers */
    BSP_GPIO_SPEC(GPIO_DEV_H, 1,  BSP_DOUT_FLAGS), /* PH1  - trolley_enable_drv */
    BSP_GPIO_SPEC(GPIO_DEV_A, 5,  BSP_DOUT_FLAGS), /* PA5  - pwr_on_off */
    BSP_GPIO_SPEC(GPIO_DEV_K, 1,  BSP_DOUT_FLAGS), /* PK1  - k3_1_drv */
    BSP_GPIO_SPEC(GPIO_DEV_K, 2,  BSP_DOUT_FLAGS), /* PK2  - k3_2_drv */
    BSP_GPIO_SPEC(GPIO_DEV_K, 3,  BSP_DOUT_FLAGS), /* PK3  - k4_drv */
    BSP_GPIO_SPEC(GPIO_DEV_I, 4,  BSP_DOUT_FLAGS), /* PI4  - k5_drv */
    BSP_GPIO_SPEC(GPIO_DEV_I, 5,  BSP_DOUT_FLAGS), /* PI5  - k6_drv */
    BSP_GPIO_SPEC(GPIO_DEV_I, 6,  BSP_DOUT_FLAGS), /* PI6  - k8_1_drv */
    BSP_GPIO_SPEC(GPIO_DEV_I, 7,  BSP_DOUT_FLAGS), /* PI7  - k8_2_drv */
    BSP_GPIO_SPEC(GPIO_DEV_I, 9,  BSP_DOUT_FLAGS), /* PI9  - k9_drv */
    BSP_GPIO_SPEC(GPIO_DEV_I, 10, BSP_DOUT_FLAGS), /* PI10 - k10_drv */
    BSP_GPIO_SPEC(GPIO_DEV_I, 11, BSP_DOUT_FLAGS), /* PI11 - k11_drv */
    BSP_GPIO_SPEC(GPIO_DEV_I, 12, BSP_DOUT_FLAGS), /* PI12 - k12_drv */

    /* LED indicators */
    BSP_GPIO_SPEC(GPIO_DEV_B, 10, BSP_DOUT_FLAGS), /* PB10 - led_s1_sys_on */
    BSP_GPIO_SPEC(GPIO_DEV_B, 11, BSP_DOUT_FLAGS), /* PB11 - led_s2_sys_on */
    BSP_GPIO_SPEC(GPIO_DEV_C, 8,  BSP_DOUT_FLAGS), /* PC8  - dbg_led0 */
    BSP_GPIO_SPEC(GPIO_DEV_C, 9,  BSP_DOUT_FLAGS), /* PC9  - dbg_led1 */
    BSP_GPIO_SPEC(GPIO_DEV_C, 10, BSP_DOUT_FLAGS), /* PC10 - dbg_led2 */
    BSP_GPIO_SPEC(GPIO_DEV_C, 15, BSP_DOUT_FLAGS), /* PC15 - led_pac230v_on */
    BSP_GPIO_SPEC(GPIO_DEV_D, 2,  BSP_DOUT_FLAGS), /* PD2  - led_grid_pwr_in */
    BSP_GPIO_SPEC(GPIO_DEV_D, 3,  BSP_DOUT_FLAGS), /* PD3  - led_ups_in */
    BSP_GPIO_SPEC(GPIO_DEV_D, 4,  BSP_DOUT_FLAGS), /* PD4  - led_system_on */
    BSP_GPIO_SPEC(GPIO_DEV_D, 5,  BSP_DOUT_FLAGS), /* PD5  - led_s2_solo_sys */
    BSP_GPIO_SPEC(GPIO_DEV_D, 6,  BSP_DOUT_FLAGS), /* PD6  - led_trolley_connected */
    BSP_GPIO_SPEC(GPIO_DEV_D, 7,  BSP_DOUT_FLAGS), /* PD7  - led_is_pc_on */
    BSP_GPIO_SPEC(GPIO_DEV_D, 11, BSP_DOUT_FLAGS), /* PD11 - led_apphost_on */

    /* Trolley / mains status outputs */
    BSP_GPIO_SPEC(GPIO_DEV_I, 13, BSP_DOUT_FLAGS), /* PI13 - trolley_connected_mcu */
    BSP_GPIO_SPEC(GPIO_DEV_I, 14, BSP_DOUT_FLAGS), /* PI14 - trolley_connected_is_pc */
    BSP_GPIO_SPEC(GPIO_DEV_J, 11, BSP_DOUT_FLAGS), /* PJ11 - drv_is_pc_site_on */
    BSP_GPIO_SPEC(GPIO_DEV_J, 12, BSP_DOUT_FLAGS), /* PJ12 - drv_app_host_site_on */
    BSP_GPIO_SPEC(GPIO_DEV_J, 13, BSP_DOUT_FLAGS), /* PJ13 - mains_connected_apphost */
    BSP_GPIO_SPEC(GPIO_DEV_J, 14, BSP_DOUT_FLAGS), /* PJ14 - mains_connected_is_pc */

    /* External watchdog — MAX6703A WDI */
    BSP_GPIO_SPEC(GPIO_DEV_H, 9,  BSP_DOUT_FLAGS), /* PH9  - MAX6703A WDI */

};

/* Digital Input pin table */
static const struct bsp_gpio_spec pinCfgDin[] = {
    /* Status inputs */
    BSP_GPIO_SPEC(GPIO_DEV_H, 5,  BSP_DIN_FLAGS), /* PH5  - grid_main_relay_status */
    BSP_GPIO_SPEC(GPIO_DEV_H, 6,  BSP_DIN_FLAGS), /* PH6  - me_box_error */
    BSP_GPIO_SPEC(GPIO_DEV_A, 0,  BSP_DIN_FLAGS), /* PA0  - trolley_connected */
    BSP_GPIO_SPEC(GPIO_DEV_B, 12, BSP_DIN_FLAGS), /* PB12 - temp_alert */


    /* PWR/CP status LEDs (input to MCU for monitoring) */
    BSP_GPIO_SPEC(GPIO_DEV_C, 6,  BSP_DIN_FLAGS), /* PC6  - led_pwr_24_on */
    BSP_GPIO_SPEC(GPIO_DEV_C, 7,  BSP_DIN_FLAGS), /* PC7  - led_cp_24v_on */

    /* System control inputs */
    BSP_GPIO_SPEC(GPIO_DEV_J, 0,  BSP_DIN_FLAGS), /* PJ0  - system_on_off */
    BSP_GPIO_SPEC(GPIO_DEV_J, 1,  BSP_DIN_FLAGS), /* PJ1  - system_reset */
    BSP_GPIO_SPEC(GPIO_DEV_J, 2,  BSP_DIN_FLAGS), /* PJ2  - s1_system_config */
    BSP_GPIO_SPEC(GPIO_DEV_J, 3,  BSP_DIN_FLAGS), /* PJ3  - s2_system_config */
    BSP_GPIO_SPEC(GPIO_DEV_J, 4,  BSP_DIN_FLAGS), /* PJ4  - solo_system_config */
    BSP_GPIO_SPEC(GPIO_DEV_J, 5,  BSP_DIN_FLAGS), /* PJ5  - trolley_connected */
    BSP_GPIO_SPEC(GPIO_DEV_J, 6,  BSP_DIN_FLAGS), /* PJ6  - is_pc_on */
    BSP_GPIO_SPEC(GPIO_DEV_J, 7,  BSP_DIN_FLAGS), /* PJ7  - app_host_on */
    BSP_GPIO_SPEC(GPIO_DEV_J, 8,  BSP_DIN_FLAGS), /* PJ8  - smart_whs_indicate */
    BSP_GPIO_SPEC(GPIO_DEV_J, 9,  BSP_DIN_FLAGS), /* PJ9  - drawer_indicate */
    BSP_GPIO_SPEC(GPIO_DEV_J, 10, BSP_DIN_FLAGS), /* PJ10 - smart_ctrl_whs_search */
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

/* DIN polling: k_timer fires every 1 ms (ISR context), k_work defers bspDinUpdate to thread context */
static struct k_timer  dinTimer;
static struct k_work   dinWork;

static void din_timer_cb(struct k_timer *timer)
{
	(void)k_work_submit(&dinWork);
}

static void din_work_cb(struct k_work *work)
{
	bspDinUpdate();
}

/*----------------------------------------------------------------------------*/
/*! @brief Digital input pins initialization */
/*----------------------------------------------------------------------------*/
static void bspDinInit(void)
{
	for (uint8_t i = 0; i < dinMax; i++) {
		if (!device_is_ready(pinCfgDin[i].port)) {
			/* If a GPIO controller isn't ready, we just skip configuring that pin. */
			continue;
		}
		(void)gpio_pin_configure(pinCfgDin[i].port, pinCfgDin[i].pin, pinCfgDin[i].flags);
	}

	k_work_init(&dinWork, din_work_cb);
	k_timer_init(&dinTimer, din_timer_cb, NULL);
	k_timer_start(&dinTimer, K_MSEC(1), K_MSEC(1));
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

/* ==================== Periodic DOUT update ====================
 *
 * Mirrors input states to status output pins (external indicator drivers).
 * Relay drivers (K3-K12, pwr_on_off, trolley_enable) and panel LEDs
 * are controlled by the state machine in psu_sm.c.
 * Fan control is handled via bsp_pwm (PWM channels).
 *
 * Called periodically from main loop (e.g., every 10 ms).
 */

void bspDoutUpdate(void)
{
	bool trolley_ok  = bspDinGet(DIN_TROLLEY_CONNECTED);
	bool is_pc_on    = bspDinGet(DIN_IS_PC_ON);
	bool app_host_on = bspDinGet(DIN_APP_HOST_ON);

	bspDoutSet(DOUT_TROLLEY_CONNECTED_MCU,   trolley_ok);
	bspDoutSet(DOUT_TROLLEY_CONNECTED_IS_PC, is_pc_on);
	bspDoutSet(DOUT_DRV_IS_PC_SITE_ON,       is_pc_on);
	bspDoutSet(DOUT_DRV_APP_HOST_SITE_ON,    app_host_on);
	bspDoutSet(DOUT_MAINS_CONNECTED_APPHOST, app_host_on);
	bspDoutSet(DOUT_MAINS_CONNECTED_IS_PC,   is_pc_on);
}

/* ==================== MAX6703A WDI ====================
 *
 * External watchdog IC (MAX6703A) on PH9.
 * Timeout: 1.6s typ. WDI is edge-sensitive; toggling within the timeout
 * window prevents RESET assertion. Called every 500ms from main loop.
 */

void bspWdiFeed(void)
{
	static bool wdi_state;

	wdi_state = !wdi_state;
	bspDoutSet(DOUT_WDI, wdi_state);
}