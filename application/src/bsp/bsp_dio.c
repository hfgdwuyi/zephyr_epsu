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

/* C standard library */
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>

/* Zephyr */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

/* BSP */
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

const uint8_t dinMax = DIN_MAX;

/* ---- Bitmap state ----
 * din_state:  sampled DIN levels, one bit per channel (updated by bspDinUpdate)
 * dout_state: requested DOUT levels, one bit per channel (set by state machine)
 */
/* volatile: din_state written by workqueue (bspDinUpdate), read by sm_thread;
 * prevents compiler caching across threads. dout_state is written by
 * sm_thread + max6703a workqueue, so it is atomic_t for safe RMW.
 *
 * dout_state spans two 32-bit atomic words: DOUT_MAX = 33 pins (indices
 * 0..32) exceeds one 32-bit word. Bits < 32 live in word 0; bit 32
 * (DOUT_MAINS_CONNECTED_MCU) in word 1. atomic_t on this 32-bit target is
 * itself 32-bit, so two words provide the full 64-bit bitmap through the
 * existing 32-bit atomic bit API. */
static volatile uint32_t din_state;
static atomic_t dout_state[2];

static bspDinSettings_t din_settings[DIN_MAX];

/* ==================== bspDioInit ==================== */

void bspDioInit(void)
{
	bspDinInit();
	bspDoutInit();
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
}

/* Sample all DIN pins into the bitmap (debounced). Call periodically
 * from the scheduler (e.g. every 1 ms). */
void bspDinUpdate(void)
{
	static uint32_t debounceTime[DIN_MAX] = { 0 };

	for (uint8_t i = 0; i < DIN_MAX; i++) {
		if (!gpio_is_ready_dt(&din_specs[i])) {
			continue;
		}

		const int    v   = gpio_pin_get_dt(&din_specs[i]);
		const bool   cur = (v > 0);
		const bool   old = (din_state & BIT(i)) != 0U;
		const uint8_t deb = din_settings[i].deb_en
				   ? din_settings[i].deb_time : 0U;

		if (old != cur) {
			debounceTime[i]++;
			if (debounceTime[i] >= deb) {
				if (cur) {
					din_state |= BIT(i);
				} else {
					din_state &= ~BIT(i);
				}
				debounceTime[i] = 0U;
			}
		} else {
			debounceTime[i] = 0U;
		}
	}
}

uint32_t bspDinGetBitmap(void)
{
	return din_state;
}

void bspDinSetDebouncing(uint8_t pin, bspDinSettings_t settings)
{
	if (pin < DIN_MAX) {
		din_settings[pin] = settings;
	}
}

/* ==================== DOUT init + bitmap + set / read ==================== */

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

/* Set/clear a DOUT flag in the bitmap — internal per-bit helper.
 * Dispatches bit < 32 to word 0, bit >= 32 to word 1. Atomic RMW so
 * concurrent writers (state machine + max6703a workqueue) cannot lose a
 * bit update. */
static void bspDoutSetBit(uint8_t pin, bool state)
{
	if (pin < DOUT_MAX) {
		atomic_set_bit_to(&dout_state[pin / 32], pin % 32, state);
	}
}

static bool bspDoutGetBit(uint8_t pin)
{
	if (pin >= DOUT_MAX) {
		return false;
	}
	return atomic_test_bit(&dout_state[pin / 32], pin % 32);
}

/* Set/clear every DOUT bit selected by the 64-bit mask. Robust to
 * non-contiguous pin groups. This is the sole public write entry. */
void bspDoutSetBitmap(uint64_t mask, bool state)
{
	for (uint8_t i = 0; i < DOUT_MAX; i++) {
		if (mask & BIT64(i)) {
			bspDoutSetBit(i, state);
		}
	}
}

/* Read the whole DOUT bitmap as the per-bit logical view — the public
 * read counterpart of bspDoutSetBitmap. */
uint64_t bspDoutGetBitmap(void)
{
	uint64_t bitmap = 0;

	for (uint8_t i = 0; i < DOUT_MAX; i++) {
		if (bspDoutGetBit(i)) {
			bitmap |= BIT64(i);
		}
	}
	return bitmap;
}

/* Apply the output bitmap to the GPIO pins. Call periodically
 * from the scheduler (e.g. every 1 ms). Uses one atomic snapshot per
 * 32-bit word (two reads for the 64-bit bitmap) — do NOT route through
 * bspDoutGetBitmap(): a bit-by-bit read could tear across the commit and
 * glitch a group transition. A torn cross-word read only defers the group
 * transition by one 1ms commit, harmless for relay/LED loads. */
void bspDoutUpdate(void)
{
	atomic_val_t lo = atomic_get(&dout_state[0]);
	atomic_val_t hi = atomic_get(&dout_state[1]);

	for (uint8_t i = 0; i < DOUT_MAX; i++) {
		if (!gpio_is_ready_dt(&dout_specs[i])) {
			continue;
		}
		const atomic_val_t word = (i < 32) ? lo : hi;
		const bool want = ((word >> (i % 32)) & 1U) != 0U;
		gpio_pin_set_dt(&dout_specs[i], want ? 1 : 0);
	}
}

