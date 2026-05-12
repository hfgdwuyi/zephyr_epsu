/*!
 * Copyright © Siemens Healthcare GmbH 2022, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 * @file
 * @brief          Implementation of basic control functions for watchdog
 */
/*----------------------------------------------------------------------------*/

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/sys/printk.h>
#include <stdbool.h>

// Project includes
// #include "terminal.h"
#include "bsp_wtdg.h"
// #include "error.h"


#ifdef WATCHDOG_TEST
#include <stdio.h>
#include <string.h>
#include "terminal.h"
#endif

#ifdef WTDG_DISABLED
#warning Watchdog triggering is disabled!
#else
/*! WTDGpin pin assignment */
// static const pinCfg wtdgPinCfg = { GPIOH, { GPIO_PIN_15, GPIO_MODE_OUTPUT_PP, GPIO_PULLUP, GPIO_SPEED_FREQ_VERY_HIGH, 0 } };
/*! Stop watchdog flag. When set to true watchdog should be stopped */
#endif
static bool wtdgStopFlag;

const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));

/* channel id + error code 建议放在本模块内管理 */
static int wdt_channel_id;
static int err;

/* 先只初始化 flags；window/callback 在 WTDG_Init() 里根据宏再填 */
static struct wdt_timeout_cfg wdt_config = {
    /* Reset SoC when watchdog timer expires. */
    .flags = WDT_FLAG_RESET_SOC,
};

/*
 * To use this sample the devicetree's /aliases must have a 'watchdog0' property.
 */
#if DT_HAS_COMPAT_STATUS_OKAY(st_stm32_window_watchdog)
#define WDT_MAX_WINDOW  100U
#elif DT_HAS_COMPAT_STATUS_OKAY(nordic_nrf_wdt)
/* Nordic supports a callback, but it has 61.2 us to complete before
 * the reset occurs, which is too short for this sample to do anything
 * useful.  Explicitly disallow use of the callback.
 */
#define WDT_ALLOW_CALLBACK 0
#elif DT_HAS_COMPAT_STATUS_OKAY(adi_max42500_watchdog)
#define WDT_ALLOW_CALLBACK 0
#define WDT_MAX_WINDOW 128U
#define WDT_OPT 0
#elif DT_HAS_COMPAT_STATUS_OKAY(raspberrypi_pico_watchdog)
#define WDT_ALLOW_CALLBACK 0
#elif DT_HAS_COMPAT_STATUS_OKAY(gd_gd32_wwdgt)
#define WDT_MAX_WINDOW 24U
#define WDT_MIN_WINDOW 18U
#define WDG_FEED_INTERVAL 12U
#elif DT_HAS_COMPAT_STATUS_OKAY(intel_tco_wdt)
#define WDT_ALLOW_CALLBACK 0
#define WDT_MAX_WINDOW 3000U
#elif DT_HAS_COMPAT_STATUS_OKAY(nxp_fs26_wdog)
#define WDT_MAX_WINDOW  1024U
#define WDT_MIN_WINDOW	320U
#define WDT_OPT 0
#define WDG_FEED_INTERVAL (WDT_MIN_WINDOW + ((WDT_MAX_WINDOW - WDT_MIN_WINDOW) / 4))
#elif DT_HAS_COMPAT_STATUS_OKAY(renesas_ra_wdt)
#define WDT_ALLOW_CALLBACK 0
#elif DT_HAS_COMPAT_STATUS_OKAY(wch_iwdg)
#define WDT_ALLOW_CALLBACK 0
#define WDT_OPT            0
#elif DT_HAS_COMPAT_STATUS_OKAY(sifli_sf32lb_wdt)
#define WDT_ALLOW_CALLBACK 0
#define WDT_OPT            0
#elif DT_HAS_COMPAT_STATUS_OKAY(andestech_atcwdt200)
#define WDT_MAX_WINDOW 500U
#endif

#ifndef WDT_ALLOW_CALLBACK
#define WDT_ALLOW_CALLBACK 1
#endif

#ifndef WDT_MAX_WINDOW
#define WDT_MAX_WINDOW  1000U
#endif

#ifndef WDT_MIN_WINDOW
#define WDT_MIN_WINDOW  0U
#endif

#ifndef WDG_FEED_INTERVAL
#define WDG_FEED_INTERVAL 50U
#endif

#ifndef WDT_OPT
#define WDT_OPT 0
#endif

#if WDT_ALLOW_CALLBACK
static void wdt_callback(const struct device *wdt_dev, int channel_id)
{
	static bool handled_event;

	if (handled_event) {
		return;
	}

	wdt_feed(wdt_dev, channel_id);

	printk("Handled things..ready to reset\n");
	handled_event = true;
}
#endif /* WDT_ALLOW_CALLBACK */

/*----------------------------------------------------------------------------*/
/*!
 *  @brief          Watchdog initialization
 *                  To calculate values for watchdog initialization use:
 *                  Reload = ((Time_needed * 32000)/(Prescaler * 1000)) -1
 *
 */
/*----------------------------------------------------------------------------*/
void WTDG_Init(void)
{
#ifndef WTDG_DISABLED
    if (!device_is_ready(wdt)) {
        printk("%s: device not ready.\n", wdt->name);
        return;
    }

    /* 在这里再使用宏，就不会受“宏定义在后面”的影响 */
    wdt_config.window.min = WDT_MIN_WINDOW;
    wdt_config.window.max = WDT_MAX_WINDOW;

#if WDT_ALLOW_CALLBACK
    wdt_config.callback = wdt_callback;
    printk("Attempting to test pre-reset callback\n");
#else
    wdt_config.callback = NULL;
    printk("Callback in RESET_SOC disabled for this platform\n");
#endif

wdt_channel_id = wdt_install_timeout(wdt, &wdt_config);
    if (wdt_channel_id == -ENOTSUP) {
        /* IWDG driver for STM32 doesn't support callback */
        printk("Callback support rejected, continuing anyway\n");
        wdt_config.callback = NULL;
        wdt_channel_id = wdt_install_timeout(wdt, &wdt_config);
    }
    if (wdt_channel_id < 0) {
        printk("Watchdog install error (%d)\n", wdt_channel_id);
        return;
    }

    err = wdt_setup(wdt, WDT_OPT);
    if (err < 0) {
        printk("Watchdog setup error (%d)\n", err);
        return;
    }

#if WDT_MIN_WINDOW != 0
    k_msleep(WDT_MIN_WINDOW);
#endif

#endif
}

/*----------------------------------------------------------------------------*/
/*!
 * @brief          Feed the watchdog
 *
 */
/*----------------------------------------------------------------------------*/
void WTDG_Feed(void)
{
#ifndef WTDG_DISABLED
    if (wtdgStopFlag) {
        return;
    }

    if (!device_is_ready(wdt)) {
        return;
    }

    /* wdt_install_timeout() 失败时 channel_id 为负 */
    if (wdt_channel_id < 0) {
        return;
    }

    (void)wdt_feed(wdt, wdt_channel_id);
#endif
}

/*----------------------------------------------------------------------------*/
/*!
 * @brief          Stops watchdog triggering
 *
 */
/*----------------------------------------------------------------------------*/
void wtdgStop(void)
{
    wtdgStopFlag = true;
}

#ifdef WATCHDOG_TEST

/*----------------------------------------------------------------------------*/
/*!
 * @brief          Stops watchdog triggering
 *
 */
/*----------------------------------------------------------------------------*/
static terminalRet terminalWatchdogTest(uint8_t argc, char **argv)
{
    if (argc < 2)
    {
        return SHELL_EARGC;
    }
    if (strcmp(argv[1], "stop") == 0)
    {
        wtdgStop();
    }
    else
    {
        return SHELL_EARG;
    }
    return SHELL_OK;
}

/* ---------------------------------------------------------------------------*/
/*!
 * @brief        Adds watchdog command to the list of terminal commands
 *
 */
/*----------------------------------------------------------------------------*/
__attribute__((constructor)) void terminalWatchdogTestInit(void)
{
    static terminalItem testItem = { .name     = "wtdg",
                                     .desc     = "Watchdog test",
                                     .help     = "Usage: \n\n "
                                                 "wtdg stop  - Stops watchdog triggering \n",
                                     .callback = terminalWatchdogTest,
                                     .next     = NULL };
    terminalAddItem(&testItem);
}
#endif
//--------------------------------- End Of File -------------------------------/
