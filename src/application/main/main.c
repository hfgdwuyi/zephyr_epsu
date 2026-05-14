#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/can.h>
#include <stdio.h>

#include <zephyr/net/tls_credentials.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include "zephyr/device.h"
#include "zephyr/sys/util.h"
#include <zephyr/drivers/led.h>
#include <zephyr/data/json.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/net/net_config.h>
#include <zephyr/logging/log.h>

#include "bsp_led.h"
#include "bsp_wtdg.h"
#include "bsp_board.h"
#include "bsp_ain.h"
#include "crc_calc.h"
#include "timing.h"

#include "http_api.h"


static void blinkHandler(void)
{
    static bool led_state = true;

    ledToggle(SYSTEM_OK_LED_NUM);
    led_state = !led_state;
	printk("LED state: %s\n", led_state ? "ON" : "OFF");
}


static void readADCHandler(void)
{
    /* Poll/refresh analog input measurements periodically */
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

    /* Initialize network interfaces (IP, DHCP/static config, etc.) */
    (void)net_config_init_app(NULL, "Initializing network");

    /* Start the HTTP server (REST endpoints are registered elsewhere) */
    http_api_start();

    /* Create a cyclic timer to blink the system OK LED */
    static timingTimer blinkTimer;
    timingAddTimer(&blinkTimer, TIMING_TIMER_CYCLIC, 500, blinkHandler);

    /* Create a cyclic timer to poll ADC/analog inputs */
    static timingTimer readADCTimer;
    timingAddTimer(&readADCTimer, TIMING_TIMER_CYCLIC, 1000, readADCHandler);

    /* Main loop: keep feeding the external watchdog */
    while (1) {
        WTDG_Feed();
    }

    return 0;
}





