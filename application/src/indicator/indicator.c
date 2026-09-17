/*!
 * @file indicator.c
 * @brief Status indicator (DAC1_OUT2 / PA5) - implementation (see indicator.h)
 */
/*----------------------------------------------------------------------------*/

/* Zephyr */
#include <zephyr/kernel.h>

/* BSP */
#include "bsp_aout.h"   /* bspAoutWrite / AOUT_PWR_ON_OFF */

/* Application */
#include "indicator.h"

/* Breathing waveform parameters */
typedef enum {
	INDICATOR_ON_MV     = 1500,   /* on level / breathing peak (mV) */
	INDICATOR_PERIOD_MS = 4000,   /* breathing period */
	INDICATOR_HALF_MS   = 2000,   /* half period: fade in / out, 2 s each */
} indicatorWaveParam_t;

static indicatorMode_t indicator_mode = INDICATOR_BREATH;   /* boot default: breathing */
static bool           indicator_breath_fast;              /* on/off key held */

void indicatorSetMode(indicatorMode_t mode)
{
	indicator_mode = mode;

	/* Apply immediately so no stale value is left before the next update */
	if (mode == INDICATOR_ON) {
		bspAoutWrite(AOUT_PWR_ON_OFF, (int16_t)INDICATOR_ON_MV);
	} else if (mode == INDICATOR_OFF) {
		bspAoutWrite(AOUT_PWR_ON_OFF, 0);
	}
}

indicatorMode_t indicatorGetMode(void)
{
	return indicator_mode;
}

void indicatorSetBreathFast(bool fast)
{
	indicator_breath_fast = fast;
}

/* Triangle breathing: 0 -> 1500 -> 0; phase from k_uptime_get_32() (no drift) */
static void indicatorBreath(uint32_t period_ms)
{
	const uint32_t half = period_ms / 2U;
	const uint32_t phase = k_uptime_get_32() % period_ms;
	int32_t mv;

	if (phase < half) {
		mv = ((int32_t)INDICATOR_ON_MV * (int32_t)phase) / (int32_t)half;
	} else {
		mv = ((int32_t)INDICATOR_ON_MV * (int32_t)(period_ms - phase)) / (int32_t)half;
	}

	bspAoutWrite(AOUT_PWR_ON_OFF, (int16_t)mv);
}

void indicatorUpdate(void)
{
	if (indicator_mode == INDICATOR_MANUAL) {
		return;   /* driven by bspAoutWrite() directly */
	}

	/* While the on/off key is held: breathe at double frequency (key feedback) */
	if (indicator_breath_fast) {
		indicatorBreath((uint32_t)INDICATOR_PERIOD_MS / 2U);
		return;
	}

	switch (indicator_mode) {
	case INDICATOR_OFF:
		bspAoutWrite(AOUT_PWR_ON_OFF, 0);
		return;

	case INDICATOR_ON:
		bspAoutWrite(AOUT_PWR_ON_OFF, (int16_t)INDICATOR_ON_MV);
		return;

	case INDICATOR_BREATH:
	default:
		indicatorBreath((uint32_t)INDICATOR_PERIOD_MS);
		return;
	}
}
