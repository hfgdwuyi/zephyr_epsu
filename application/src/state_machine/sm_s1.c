/*!
 * @file sm_s1.c
 * @brief S1 系统状态机实现（见 sm_s1.h）
 *
 * 管脚处理：直接操作 BSP 的位图接口 bspDoutSetBitmap()，在各自子状态中
 * 指定位置调用 —— 没有中间状态表，也没有通用 apply 包装：
 *
 *   relayWrite(S1_T0_RELAY);                // 继电器电平：要的写 1，其余写 0
 *   ledWrite(...);                          // LED 电平：同上
 *
 * 每个状态写出自己那一份完整电平，不做"整组复位再重设"：
 * 本状态仍然要合的路（例如 T2 里的 K3/K13）不会被中途拉掉，
 * 只把本状态不需要的路明确写 0。relay 与 led 写法完全一致，不区分系统归属。
 * K2~K7 是交流继电器，K8_1/K8_2/K9~K13 是 efuse。T2/T3 上电时两组
 * 同时起步、各自内部依次延时 10ms（md："AC relays delay each other by
 * 10 ms; DC efuse enables delay each other by 10 ms"）—— staging 因此是
 * 两条并行的轨道，每 10ms 各直接 bspDoutSetBitmap(BIT64(pin), true) 一路。
 *
 * WDI(PH9) 归 max6703a 模块，本文件一律不触碰。
 */
/*----------------------------------------------------------------------------*/
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "state_machine.h"   /* 管脚定义 + 电平写入（S1/S2 共用） */
#include "sm_s1.h"

/* ==================== 各子状态的继电器集合 ====================
 * 每个状态要合哪几路继电器，在这里显式写出来（与 md 的 Relay sequence 对应）：
 *   K2~K7        = 交流继电器（AC）
 *   K8_1/K8_2/K9~K13 = efuse
 */
#define S1_T0_RELAY  (K3 | K13)                                   /* 待机回路 */
#define S1_T1_RELAY  (K3 | K13)                                   /* 市电掉电仍保持待机回路 */
#define S1_T2_RELAY  (K3 | K4 | K5 | K6 | K7 |                    \
		      K8_1 | K9 | K11 | K12 | K13)               /* 开机 */
#define S1_T3_RELAY  (K2 | K6 | K7 |                              \
		      K8_1 | K9 | K10 | K11 | K12 | K13)         /* OR 模式 */
#define S1_T4_RELAY  (K3 | K13)                                   /* 关机：+ K9/K10 动态 */

/* 状态名（仅本文件打印用） */
static const char *s1StateName(smS1State_t st);

/* ==================== 内部状态 ==================== */

#define S1_STAGE_STEP_MS 10

/* T2 / T3 的继电器上电次序 —— 两条轨道，同时起步，各自内部 10ms 依次延时
 *   ac  : K2~K7   交流继电器
 *   ef  : K8_1/K8_2/K9~K13  efuse
 */
static const uint8_t s1_t2_ac[] = {
	DOUT_K3_DRV, DOUT_K4_DRV, DOUT_K5_DRV, DOUT_K6_DRV, DOUT_K7_DRV,
};
static const uint8_t s1_t2_ef[] = {
	DOUT_K8_1_EN, DOUT_K9_EN, DOUT_K11_EN, DOUT_K12_EN, DOUT_K13_EN,
};

static const uint8_t s1_t3_ac[] = {
	DOUT_K2_DRV, DOUT_K6_DRV, DOUT_K7_DRV,
};
static const uint8_t s1_t3_ef[] = {
	DOUT_K8_1_EN, DOUT_K9_EN, DOUT_K10_EN, DOUT_K11_EN, DOUT_K12_EN,
	DOUT_K13_EN,
};

static smS1State_t s1_state = SM_S1_T0;
static int64_t     s1_entry_ms;
static uint32_t    s1_prev_din;
static bool        s1_prev_valid;   /* 首个 tick 不做边沿判定（无历史值） */

/* 顺序上电（staging）：AC 轨与 efuse 轨并行推进，终态 = s1_stage_target */
static bool     s1_staging_active;
static uint64_t s1_stage_target;
static const uint8_t *s1_stage_ac;   /* 交流继电器轨 */
static size_t   s1_stage_ac_n;
static size_t   s1_stage_ac_idx;
static const uint8_t *s1_stage_ef;   /* efuse 轨 */
static size_t   s1_stage_ef_n;
static size_t   s1_stage_ef_idx;
static int64_t  s1_stage_ms;

/* ==================== 输入判定 ==================== */

/* ME_BOX_ERROR 高 = 市电/整机正常；低 = 市电掉电或故障 */
static inline bool inMainsOk(uint32_t din)
{
	return (din & BIT(DIN_ME_BOX_ERROR)) != 0U;
}

static inline bool inTrolley(uint32_t din)
{
	return (din & BIT(DIN_TROLLEY_CONNECTED)) != 0U;
}

static inline bool inOnOff(uint32_t din)
{
	return (din & BIT(DIN_SYSTEM_ON_OFF)) != 0U;
}

static inline bool inReset(uint32_t din)
{
	return (din & BIT(DIN_SYSTEM_RESET)) != 0U;
}

/* 顺序上电（定义见后）：先全断，再按 AC / efuse 两条轨道逐路合上 target */
static void relayStage(uint64_t target,
			const uint8_t *ac, size_t ac_n,
			const uint8_t *ef, size_t ef_n);

/* ==================== 各子状态的管脚输出 ==================== */
/* 管脚号直接写在各状态自己的函数里，改哪一路就改哪一行 */

/* T0 上电待机：K3 + K13 待机回路 + 供电指示 */
static void s1OutputT0(void)
{
	relayWrite(S1_T0_RELAY);
	ledWrite(L_GRID_IN | L_PWR24 | L_CP224 | L_TROLLEY |
		 D_MAINS_MCU | D_MAINS_IS_PC);
}

/* T1 市电掉电 / OR 关机：继电器保持待机回路（同 T0，市电掉电也能进待机）
 * LED：md 未给出该状态的点亮要求 → 全灭（若需与 T0 一致请说明） */
static void s1OutputT1(void)
{
	relayWrite(S1_T1_RELAY);
	ledWrite(0);
}

/* T2 系统开机：LED 立即置位；继电器按 10ms 逐路合上
 * 目标集合：K3,K4,K5,K6,K7,K8_1,K9,K11,K12,K13 */
static void s1OutputT2(void)
{
	ledWrite(L_GRID_IN | L_PWR24 | L_CP224 | L_TROLLEY |
		 D_MAINS_MCU | D_MAINS_IS_PC |
		 D_IS_PC_SITE | D_APP_HOST | D_TROLLEY_EN);
	/* 继电器：目标 S1_T2_RELAY，本状态不要的路写 0，其余由两轨 10ms 逐路合 */
	relayStage(S1_T2_RELAY,
		    s1_t2_ac, ARRAY_SIZE(s1_t2_ac),
		    s1_t2_ef, ARRAY_SIZE(s1_t2_ef));
}

/* T3 开机后市电掉电（OR 模式）：K2,K6,K7,K8_1,K9,K10,K11,K12,K13 */
static void s1OutputT3(void)
{
	ledWrite(L_UPS_IN | L_PWR24 | L_CP224 | L_TROLLEY |
		 D_IS_PC_SITE | D_APP_HOST);
	/* 继电器：目标 S1_T3_RELAY */
	relayStage(S1_T3_RELAY,
		    s1_t3_ac, ARRAY_SIZE(s1_t3_ac),
		    s1_t3_ef, ARRAY_SIZE(s1_t3_ef));
}

/* T4 正常关机：待机回路 + K9/K10 由 IS_PC_ON / APP_HOST_ON 实时决定 */
static void s1OutputT4(uint32_t din)
{
	const bool is_pc    = (din & BIT(DIN_IS_PC_ON))    != 0U;
	const bool app_host = (din & BIT(DIN_APP_HOST_ON)) != 0U;
	uint64_t   relay    = S1_T4_RELAY;
	uint64_t   led      = L_GRID_IN | L_PWR24 | L_CP224 | L_TROLLEY |
			      D_MAINS_MCU | D_MAINS_IS_PC;

	if (is_pc) {
		relay |= K9;
		led   |= D_IS_PC_SITE;
	}
	if (app_host) {
		relay |= K10;
		led   |= D_APP_HOST;
	}

	relayWrite(relay);
	ledWrite(led);
}

/* RESET 硬件复位：全部断开 */
static void s1OutputReset(void)
{
	relayWrite(0);
	ledWrite(0);
}

/* 进入某子状态时写它自己的输出 */
static void s1Output(smS1State_t st, uint32_t din)
{
	switch (st) {
	case SM_S1_T0:    s1OutputT0();     break;
	case SM_S1_T1:    s1OutputT1();     break;
	case SM_S1_T2:    s1OutputT2();     break;
	case SM_S1_T3:    s1OutputT3();     break;
	case SM_S1_T4:    s1OutputT4(din);  break;
	case SM_S1_RESET: s1OutputReset();  break;
	default:          break;
	}
}

/* ==================== 顺序上电（staging） ==================== */

/* 开始：只把本状态不需要的继电器写 0（target 里的路保持原状，不中途拉掉），
 * 然后 AC / efuse 两轨同时起步逐路合上（首次 step 在下一次 tick 立即发生） */
static void relayStage(uint64_t target,
			const uint8_t *ac, size_t ac_n,
			const uint8_t *ef, size_t ef_n)
{
	bspDoutSetBitmap(SM_RELAY_ALL & ~target, false);   /* 只拉低本状态不要的路 */

	s1_stage_target   = target;
	s1_stage_ac       = ac;
	s1_stage_ac_n     = ac_n;
	s1_stage_ac_idx   = 0;
	s1_stage_ef       = ef;
	s1_stage_ef_n     = ef_n;
	s1_stage_ef_idx   = 0;
	s1_stage_ms       = k_uptime_get() - S1_STAGE_STEP_MS;  /* 首次立即执行 */
	s1_staging_active = true;
}

/* 每 10ms：AC 轨与 efuse 轨各推进一路（同时起步、各自 10ms 递增） */
static void s1StageTick(void)
{
	if (!s1_staging_active) {
		return;
	}
	if ((k_uptime_get() - s1_stage_ms) < S1_STAGE_STEP_MS) {
		return;
	}
	s1_stage_ms = k_uptime_get();

	const bool ac_step = s1_stage_ac_idx < s1_stage_ac_n;
	const bool ef_step = s1_stage_ef_idx < s1_stage_ef_n;

	if (!ac_step && !ef_step) {
		s1_staging_active = false;
		return;
	}

	/* 只合 target 里的路（轨道数组管先后顺序） */
	if (ac_step) {
		const uint8_t pin = s1_stage_ac[s1_stage_ac_idx++];

		if ((s1_stage_target & BIT64(pin)) != 0U) {
			bspDoutSetBitmap(BIT64(pin), true);
		}
	}
	if (ef_step) {
		const uint8_t pin = s1_stage_ef[s1_stage_ef_idx++];

		if ((s1_stage_target & BIT64(pin)) != 0U) {
			bspDoutSetBitmap(BIT64(pin), true);
		}
	}
}

/* ==================== 状态切换 ==================== */

static void s1EnterState(smS1State_t st, uint32_t din)
{
	s1_state    = st;
	s1_entry_ms = k_uptime_get();
	s1_staging_active = false;

	printk("S1: -> %s\n", s1StateName(st));

	s1Output(st, din);   /* 每个子状态进入时都写自己的管脚输出 */
}

/* ==================== 对外接口 ==================== */

static const char *s1StateName(smS1State_t st)
{
	switch (st) {
	case SM_S1_T0:    return "T0 上电待机";
	case SM_S1_T1:    return "T1 市电掉电/OR关机";
	case SM_S1_T2:    return "T2 系统开机";
	case SM_S1_T3:    return "T3 开机后市电掉电";
	case SM_S1_T4:    return "T4 正常关机";
	case SM_S1_RESET: return "RESET 硬件复位";
	default:          return "?";
	}
}

void smS1Enter(void)
{
	s1_prev_valid = false;      /* 进入后首个 tick 只记录输入，不判边沿 */
	s1_prev_din   = 0;
	s1EnterState(SM_S1_T0, 0);      /* 进入初始子状态 T0，由它设置管脚 */
}

smS1State_t smS1Tick(uint32_t din)
{
	const bool mains   = inMainsOk(din);
	const bool trolley = inTrolley(din);
	const bool onoff   = inOnOff(din);
	const bool reset   = inReset(din);

	bool onoff_rise = onoff && !inOnOff(s1_prev_din);
	bool reset_rise = reset && !inReset(s1_prev_din);

	if (!s1_prev_valid) {
		onoff_rise = false;
		reset_rise = false;
		s1_prev_valid = true;
	}

	/* md 前置条件：T0/T2/T4 需 市电正常 && 推车连接；
	 *              T3    需 市电掉电 && 推车仍连接（OR 模式） */
	const bool ready   = mains && trolley;
	const bool or_mode = !mains && trolley;

	/* ---- 复位优先：md = ME_BOX_ERROR && SYSTEM_RESET 上升沿 → 全部重新初始化 ---- */
	if (reset_rise && mains) {
		s1EnterState(SM_S1_RESET, din);
		s1_prev_din = din;
		return s1_state;
	}

	/* ---- 复位态：reset 释放后（"3V3 && 0V"）市电正常回 T0，否则保持关机 ---- */
	if (s1_state == SM_S1_RESET) {
		if (!reset && (k_uptime_get() - s1_entry_ms) >= 2000) {
			s1EnterState(ready ? SM_S1_T0 : SM_S1_T1, din);
		}
		s1_prev_din = din;
		return s1_state;
	}

	/* ---- 顺序上电推进（T2/T3）---- */
	if (s1_state == SM_S1_T2 || s1_state == SM_S1_T3) {
		s1StageTick();
	}

	/* ---- 状态转换 ---- */
	switch (s1_state) {

	case SM_S1_T0:   /* 上电待机 */
		if (!ready) {
			s1EnterState(SM_S1_T1, din);      /* 市电掉电或推车断开 */
		} else if (onoff_rise) {
			s1EnterState(SM_S1_T2, din);      /* 按键开机 */
		}
		break;

	case SM_S1_T1:   /* 市电掉电 / OR 关机 */
		if (ready) {
			s1EnterState(SM_S1_T0, din);      /* 市电恢复且推车在 → 回待机 */
		}
		break;

	case SM_S1_T2:   /* 系统开机 */
		if (or_mode) {
			s1EnterState(SM_S1_T3, din);      /* 开机后市电掉电(推车仍在) → OR */
		} else if (!ready) {
			s1EnterState(SM_S1_T1, din);      /* 推车断开 → T1 */
		} else if (onoff_rise) {
			s1EnterState(SM_S1_T4, din);      /* 按键正常关机 */
		}
		break;

	case SM_S1_T3:   /* 开机后市电掉电（OR 模式） */
		if (ready) {
			s1EnterState(SM_S1_T2, din);      /* 市电恢复回开机 */
		} else if (!trolley) {
			s1EnterState(SM_S1_T1, din);      /* 推车也断开 → T1 */
		}
		break;

	case SM_S1_T4:   /* 正常关机 */
		if ((k_uptime_get() - s1_entry_ms) >= 200) {
			s1EnterState(ready ? SM_S1_T0 : SM_S1_T1, din);
		} else {
			s1OutputT4(din);            /* K9/K10 随 IS_PC/APP_HOST 实时更新 */
		}
		break;

	default:
		break;
	}

	s1_prev_din = din;
	return s1_state;
}
