#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <stdio.h>

#include "timing.h"
#include "bsp_led.h"
#include "bsp_wtdg.h"
#include "bsp_board.h"
#include "crc_calc.h"
#include "bsp_ain.h"




static void blinkHandler(void)
{
    static bool led_state = true;

    ledToggle(SYSTEM_OK_LED_NUM);
    led_state = !led_state;
	printk("LED state: %s\n", led_state ? "ON" : "OFF");
}


static void readADCHandler(void)
{
    bspAinPoll();
}

int main(void)
{
    // Initialize pins and interfaces
    boardInit();

    // Initialize external watchdog
    WTDG_Init();

    // Feed external watchdog
    WTDG_Feed();

    // Enable CRC module
    crcInit();

    static timingTimer blinkTimer;
    timingAddTimer(&blinkTimer, TIMING_TIMER_CYCLIC, 500, blinkHandler);

    static timingTimer readADCTimer;
    timingAddTimer(&readADCTimer, TIMING_TIMER_CYCLIC, 1000, readADCHandler);

    while (1) {
        WTDG_Feed();
    }

    return 0;
}





