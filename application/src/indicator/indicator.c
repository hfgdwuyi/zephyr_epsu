/*!
 * @file indicator.c
 * @brief 状态指示灯（DAC1_OUT2 / PA5）—— 应用层实现（见 indicator.h）
 */
/*----------------------------------------------------------------------------*/
/* Zephyr */
#include <zephyr/kernel.h>

/* BSP */
#include "bsp_aout.h"   /* bspAoutWrite / AOUT_PWR_ON_OFF */

/* 本模块 */
#include "indicator.h"

/* 呼吸波形参数 */
typedef enum {
	INDICATOR_ON_MV     = 1500,   /* 常亮 / 呼吸峰值 (mV) */
	INDICATOR_PERIOD_MS = 4000,   /* 呼吸周期 0.25 Hz */
	INDICATOR_HALF_MS   = 2000,   /* 半周期：渐亮 / 渐暗各 2 s */
} indicatorWaveParam_t;

static indicatorMode_t indicator_mode = INDICATOR_BREATH;   /* 上电默认呼吸（待机） */
static bool           indicator_breath_fast;              /* 开关键按下 → 呼吸频率加倍 */

void indicatorSetMode(indicatorMode_t mode)
{
	indicator_mode = mode;

	/* 立即更新一次，避免下一次 indicatorUpdate() 前残留旧值 */
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

/* 三角波呼吸：0 → 1500 → 0，相位由 k_uptime_get_32() 推算（不会累积漂移）*/
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
		return;   /* 由 bspAoutWrite 直接控制，不干预 */
	}

	/* 开关键按下期间：不论当前模式，统一走“双倍频率”的呼吸（按键反馈）*/
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
