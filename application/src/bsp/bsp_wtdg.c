/*!
 * Copyright (c) Siemens Healthcare GmbH 2022, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 * @file
 * @brief Watchdog control functions (Zephyr port)
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/sys/printk.h>
#include <zephyr/devicetree.h>
#include <stdbool.h>

#include "bsp_wtdg.h"

/* Check if watchdog0 alias exists and is okay in devicetree */
#if DT_NODE_HAS_STATUS(DT_ALIAS(watchdog0), okay)
#define WDT_AVAILABLE 1
const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
#else
#define WDT_AVAILABLE 0
const struct device *const wdt = NULL;
#endif

static bool wtdgStopFlag;
static int wdt_channel_id;
static int err;

static struct wdt_timeout_cfg wdt_config = {
    .flags = WDT_FLAG_RESET_SOC,
};

#if DT_HAS_COMPAT_STATUS_OKAY(st_stm32_window_watchdog)
#define WDT_MAX_WINDOW  100U
#elif DT_HAS_COMPAT_STATUS_OKAY(nordic_nrf_wdt)
#define WDT_ALLOW_CALLBACK 0
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
#endif

void wtdgInit(void)
{
#if WDT_AVAILABLE
    if (!device_is_ready(wdt)) {
        printk("%s: device not ready.\n", wdt->name);
        return;
    }

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
#else
    printk("wtdgInit: watchdog0 not in devicetree, skipping\n");
#endif /* WDT_AVAILABLE */
}

void wtdgFeed(void)
{
#if WDT_AVAILABLE
    if (wtdgStopFlag) {
        return;
    }

    if (!device_is_ready(wdt)) {
        return;
    }

    if (wdt_channel_id < 0) {
        return;
    }

    (void)wdt_feed(wdt, wdt_channel_id);
#endif /* WDT_AVAILABLE */
}

void wtdgStop(void)
{
    wtdgStopFlag = true;
}
