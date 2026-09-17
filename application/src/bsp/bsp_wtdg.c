/*!
 * Copyright (c) Siemens Healthcare GmbH 2022, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 * @file
 * @brief Watchdog control functions (Zephyr port)
 */

/* Standard library */
#include <stdbool.h>

/* Zephyr */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/sys/printk.h>

/* BSP */
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
static int wdt_err;

static struct wdt_timeout_cfg wdt_config = {
    .flags = WDT_FLAG_RESET_SOC,
};

#if DT_HAS_COMPAT_STATUS_OKAY(st_stm32_window_watchdog)
/* WWDG maximum window (hardware limit): t_max = 64 * 4096 * 2^WDGTB_max / f_PCLK3.
 * PCLK3 = (SYSCLK/HPRE)/2 = (480/2)/2 = 120 MHz and WDGTB max is /128,
 * so t_max ~= 279.6 ms (t_min ~= 34.1 us).
 * A larger timeout makes wdt_install_timeout() return -EINVAL (-22) before
 * wdt_setup(), so the WWDG would never start at all.
 * Therefore 250 ms (actual ~253 ms, inside the driver's 10% tolerance): within
 * the hardware limit and leaves head-room for init before the 50 ms heartbeat.
 * Cross-boot upgrade safety is provided by the external MAX6703A (1.6 s, PH9). */
#define WDT_MAX_WINDOW  250U
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

void bspWtdgInit(void)
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
#else
    wdt_config.callback = NULL;
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

    wdt_err = wdt_setup(wdt, WDT_OPT);
    if (wdt_err < 0) {
        printk("Watchdog setup error (%d)\n", wdt_err);
        return;
    }

#if WDT_MIN_WINDOW != 0
    k_msleep(WDT_MIN_WINDOW);
#endif
#else
    printk("bspWtdgInit: watchdog0 not in devicetree, skipping\n");
#endif /* WDT_AVAILABLE */
}

void bspWtdgFeed(void)
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

void bspWtdgStop(void)
{
    wtdgStopFlag = true;
}
