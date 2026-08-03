/*
 * bsp_dio.c — Digital I/O (Zephyr port, devicetree-driven)
 *
 * All GPIO pin assignments come from devicetree overlay.
 * No hardcoded GPIO port/pin macros — DT_FOREACH_CHILD generates
 * the configuration tables at compile time.
 *
 * To change a pin assignment, edit application/app.overlay:
 * the dout_config and din_config nodes define the mapping.
 */

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
 * Zephyr's GPIO_DT_SPEC_GET uses the full GPIO flags from devicetree.
 * Our dout/din binding nodes specify GPIO_ACTIVE_HIGH directly
 * in the gpios property, so the flags are built-in.  No more
 * BSP_DOUT_FLAGS / BSP_DIN_FLAGS macros needed.
 */

/* Static function declarations */
static void bspDoutInit(void);
static void bspDinInit(void);
static void bspDinUpdate(void);

/* ==================== DOUT: devicetree → gpio_dt_spec array ==================== */

#define DOUT_GPIO_SPEC(node_id) GPIO_DT_SPEC_GET(node_id, gpios),
static const struct gpio_dt_spec dout_specs[] = {
	DT_FOREACH_CHILD(DT_NODELABEL(dout_config), DOUT_GPIO_SPEC)
};

#define DOUT_MAX ARRAY_SIZE(dout_specs)
const uint8_t doutMax = DOUT_MAX;

/* ==================== DIN: devicetree → gpio_dt_spec array ==================== */

#define DIN_GPIO_SPEC(node_id) GPIO_DT_SPEC_GET(node_id, gpios),
static const struct gpio_dt_spec din_specs[] = {
	DT_FOREACH_CHILD(DT_NODELABEL(din_config), DIN_GPIO_SPEC)
};

#define DIN_MAX  ARRAY_SIZE(din_specs)
#define DIN_BYTES  ((DIN_MAX + CHAR_BIT - 1) / CHAR_BIT)

const uint8_t dinMax = DIN_MAX;
static uint8_t       dinState[DIN_BYTES];
static bspDinSettings dinSettings[DIN_MAX];

/* ==================== DIN polling (timer + work) ==================== */

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

/* ==================== bspDioInit ==================== */

/* ==================== DOUT status mirroring (10ms background timer) ==================== */

static struct k_timer mirrorTimer;

static void mirror_timer_cb(struct k_timer *timer)
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

static void mirror_timer_start(void)
{
	k_timer_init(&mirrorTimer, mirror_timer_cb, NULL);
	k_timer_start(&mirrorTimer, K_MSEC(10), K_MSEC(10));
}

void bspDioInit(void)
{
	bspDinInit();
	bspDoutInit();
	mirror_timer_start();
}

/* ==================== DIN init + update + setters ==================== */

static void bspDinInit(void)
{
	for (uint8_t i = 0; i < DIN_MAX; i++) {
		if (!gpio_is_ready_dt(&din_specs[i])) {
			continue;
		}
		gpio_pin_configure_dt(&din_specs[i], GPIO_INPUT);
	}

	k_work_init(&dinWork, din_work_cb);
	k_timer_init(&dinTimer, din_timer_cb, NULL);
	k_timer_start(&dinTimer, K_MSEC(1), K_MSEC(1));
}

static void bspDinUpdate(void)
{
	static uint32_t debounceTime[DIN_MAX] = { 0 };

	for (uint8_t i = 0; i < DIN_MAX; i++) {
		if (!gpio_is_ready_dt(&din_specs[i])) {
			continue;
		}

		const uint8_t bit  = i % CHAR_BIT;
		const uint8_t byte = i / CHAR_BIT;
		const int    v     = gpio_pin_get_dt(&din_specs[i]);
		const bool   cur   = (v > 0);
		const bool   old   = (dinState[byte] & BIT(bit)) != 0U;
		const uint8_t deb  = dinSettings[i].deb_en
				   ? dinSettings[i].deb_time : 0U;

		if (old != cur) {
			debounceTime[i]++;
			if (debounceTime[i] >= deb) {
				if (cur) {
				dinState[byte] |= BIT(bit);
			} else {
				dinState[byte] &= (uint8_t)~BIT(bit);
			}
				debounceTime[i] = 0U;
			}
		} else {
			debounceTime[i] = 0U;
		}
	}
}

void bspDinSetDebouncing(uint8_t pin, bspDinSettings settings)
{
	if (pin < DIN_MAX) {
		dinSettings[pin] = settings;
	}
}

uint8_t bspDinRead(uint8_t byte)
{
	if (byte >= DIN_BYTES) {
		return 0U;
	}
	return dinState[byte];
}

/* ==================== DOUT init + set / read ==================== */

static void bspDoutInit(void)
{
	for (uint8_t i = 0; i < DOUT_MAX; i++) {
		if (!gpio_is_ready_dt(&dout_specs[i])) {
			continue;
		}
		gpio_pin_configure_dt(&dout_specs[i], GPIO_OUTPUT_INACTIVE);
		gpio_pin_set_dt(&dout_specs[i], 0);
	}
}

void bspDoutSet(uint8_t pinNumber, bool state)
{
	if (pinNumber >= DOUT_MAX) {
		return;
	}
	if (!gpio_is_ready_dt(&dout_specs[pinNumber])) {
		return;
	}
	gpio_pin_set_dt(&dout_specs[pinNumber], state ? 1 : 0);
}

bool bspDoutRead(uint8_t pinNumber)
{
	if (pinNumber >= DOUT_MAX) {
		return false;
	}
	if (!gpio_is_ready_dt(&dout_specs[pinNumber])) {
		return false;
	}
	return gpio_pin_get_dt(&dout_specs[pinNumber]) > 0;
}


void bspWdiFeed(void)
{
	static bool wdi_state;

	wdi_state = !wdi_state;
	bspDoutSet(DOUT_WDI, wdi_state);
}
