/*!
 * Copyright © Siemens Healthcare GmbH 2022, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 * @file
 * @brief LED control functions
 */
/*----------------------------------------------------------------------------*/

/* C standard library */
#include <stdbool.h>
#include <stdint.h>

/* Zephyr */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

/* BSP */
#include "bsp_led.h"

/*! @struct Structure for LED description */
#define BSP_LED_LIST(X)           \
    X(LED_GREEN,  green_led)      \
    X(LED_YELLOW, yellow_led)
typedef enum
{
#define X(id, node) id,
    BSP_LED_LIST(X)
#undef X
    LED_PIN_COUNT
} bsp_gpio_id_t;
static const struct gpio_dt_spec ledPinCfg[LED_PIN_COUNT] =
{
#define X(id, node) [id] = GPIO_DT_SPEC_GET(DT_NODELABEL(node), gpios),
    BSP_LED_LIST(X)
#undef X
};


/*----------------------------------------------------------------------------*/
/*!
 * @brief          LEDs initialization
 *
 */
/*----------------------------------------------------------------------------*/
void bspLedInit(void)
{
    for (uint8_t i = 0; i < LED_PIN_COUNT; i++)
    {
        // Initialize pin
        if (!gpio_is_ready_dt(&ledPinCfg[i]))
        {
            return;
        }

        gpio_pin_configure_dt(&ledPinCfg[i], GPIO_OUTPUT_INACTIVE);
    }
}

/*----------------------------------------------------------------------------*/
/*!
 * @brief          Switch on the specified LED
 *
 * @param[in]      ledNumber - Index of LED to switch on
 *
 */
/*----------------------------------------------------------------------------*/
void bspLedSwitchOn(uint8_t ledNumber)
{
    if (ledNumber < LED_PIN_COUNT)
    {
        (void)gpio_pin_set_dt(&ledPinCfg[ledNumber], 1);
    }
}

/*----------------------------------------------------------------------------*/
/*!
 * @brief          Switch off the specified LED
 *
 * @param[in]      ledNumber - Index of LED to switch off
 *
 */
/*----------------------------------------------------------------------------*/
void bspLedSwitchOff(uint8_t ledNumber)
{
    if (ledNumber < LED_PIN_COUNT)
    {
        (void)gpio_pin_set_dt(&ledPinCfg[ledNumber], 0);
    }
}

/*----------------------------------------------------------------------------*/
/*!
 * @brief          Toggle the specified LED
 *
 * @param[in]      ledNumber - Index of LED to toggle
 *
 */
/*----------------------------------------------------------------------------*/
void bspLedToggle(uint8_t ledNumber)
{
    if (ledNumber < LED_PIN_COUNT)
    {
        gpio_pin_toggle_dt(&ledPinCfg[ledNumber]);
    }
}

//--------------------------------- End Of File -------------------------------/
