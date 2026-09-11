/*!
 * @file sm_s2.c
 * @brief S2 系统状态机实现（见 sm_s2.h）
 *
 * 管脚处理与 S1 完全同构：直接操作 BSP 位图接口，在各子状态中指定位置调用，
 * 没有中间状态表、没有通用 apply 包装：
 *
 *   relayWrite(relay_mask);   // 继电器电平：要的写 1、组内其余写 0
 *   ledWrite(led_mask);       // LED 电平：同上
 *
 * 管脚定义（K*、两个管脚组）来自 state_machine.h，S1/S2 共用。
 *
 * Excel（statemachine config.xlsx）的 s2 表逐行对照（表里用 PAC/PDC 命名）：
 *   on             : K4,K7,K6 + K8_1,K8_2,K9,K13,K11,K12 合；K10 = n/a（不受控）
 *   off(两种)      : 全部断，只有 K9/K11 跟随 IS_PC、K5 跟随 APP_HOST 延时断
 *   K5             : 推车连接=on / 断开=off（on 表）；off 表两个方向都做延时断
 *   PAC6           : pin_config 未填、待确认 → 当前不控制（写 0）
 */
/*----------------------------------------------------------------------------*/
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "state_machine.h"   /* 管脚定义 / 电平写入 / 输入判定（S1/S2 共用） */
#include "sm_s2.h"

/* 状态名（仅本文件打印用） */
static const char *s2StateName(smS2State_t st);

/* ==================== 内部状态 ==================== */

static smS2State_t s2_state = SM_S2_STANDBY;
static int64_t     s2_entry_ms;
static uint32_t    s2_prev_din;
static bool        s2_prev_valid;    /* 首个 tick 不做边沿判定 */
static bool        s2_solo;          /* 子模式：pj4 */

#define S2_RESET_HOLD_MS 2000

/* ==================== 各子状态的管脚输出 ==================== */

/* 子模式指示灯：solo 亮 solo+sys，classic 只亮 sys */
static uint64_t s2ModeLed(void)
{
	return L_S2_SYS_ON | (s2_solo ? L_S2_SOLO_SYS : 0ULL);
}

/* ---- ON 系统开机（Excel "s2 <mode> mode on"） ----
 * K4 K7 K6 合；K5 = 推车连接时合、断开时断
 * K8_1 K8_2 K9 K13 K11 K12 合
 * K10 = n/a（Excel 不控制）→ 保持断开
 */
static void s2OutputRun(uint32_t din)
{
	uint64_t relay = K4 | K7 | K6 |
			 K8_1 | K8_2 | K9 | K13 | K11 | K12;
	uint64_t led   = s2ModeLed() | L_SYS_ON |
			 L_PWR24 | L_CP224 | L_TROLLEY | D_TROLLEY_EN;

	if (isTrolleyConnected(din)) {
		relay |= K5;
	}
	if (isMainsOk(din)) {
		led |= L_GRID_IN | D_MAINS_MCU | D_MAINS_IS_PC;
	}
	if (isPcOn(din)) {
		led |= L_IS_PC | D_IS_PC_SITE;
	}
	if (isAppHostOn(din)) {
		led |= L_APP_HOST | D_APP_HOST;
	}

	relayWrite(relay);
	ledWrite(led);
}

/* ---- OFF（Excel "off not standby" / "off in standby" 的管脚部分相同） ----
 * 其余全断，只有两路"延时关断"：
 *   K5  ← APP_HOST_ON ：主机还在就先不断（连接/断开推车两个方向都一样）
 *   K9 / K11 ← IS_PC_ON ：PC 还在就先不断
 * LED：保留子模式指示灯；待机态（市电在场）另亮市电指示。
 */
static void s2OutputOff(uint32_t din, bool standby)
{
	uint64_t relay = 0;
	uint64_t led   = s2ModeLed();

	if (isAppHostOn(din)) {
		relay |= K5;
	}
	if (isPcOn(din)) {
		relay |= K9 | K11;
	}
	if (standby && isMainsOk(din)) {
		led |= L_GRID_IN | D_MAINS_MCU | D_MAINS_IS_PC;
	}
	if (isTrolleyConnected(din)) {
		led |= L_TROLLEY;
	}

	relayWrite(relay);
	ledWrite(led);
}

/* ---- RESET 硬件复位：全部断开 ---- */
static void s2OutputReset(void)
{
	relayWrite(0);
	ledWrite(0);
}

/* 进入某子状态时写它自己的输出 */
static void s2Output(smS2State_t st, uint32_t din)
{
	switch (st) {
	case SM_S2_RUN:          s2OutputRun(din);        break;
	case SM_S2_STANDBY:      s2OutputOff(din, true);  break;
	case SM_S2_OFF_NO_MAINS: s2OutputOff(din, false); break;
	case SM_S2_RESET:        s2OutputReset();         break;
	default:                 break;
	}
}

/* ==================== 状态切换 ==================== */

static void s2EnterState(smS2State_t st, uint32_t din)
{
	s2_state    = st;
	s2_entry_ms = k_uptime_get();

	printk("S2: -> %s\n", s2StateName(st));

	s2Output(st, din);
}

/* ==================== 对外接口 ==================== */

static const char *s2StateName(smS2State_t st)
{
	switch (st) {
	case SM_S2_RUN:          return "RUN 系统开机";
	case SM_S2_STANDBY:      return "STANDBY 关机·待机";
	case SM_S2_OFF_NO_MAINS: return "OFF 关机·非待机";
	case SM_S2_RESET:        return "RESET 硬件复位";
	default:                 return "?";
	}
}

void smS2Enter(void)
{
	s2_prev_valid = false;
	s2_prev_din   = 0;
	s2_solo       = false;
	s2EnterState(SM_S2_STANDBY, 0);
}

smS2State_t smS2Tick(uint32_t din)
{
	const bool mains   = isMainsOk(din);
	const bool reset   = isResetActive(din);

	bool onoff_rise = isOnOffActive(din) && !isOnOffActive(s2_prev_din);
	bool reset_rise = reset && !isResetActive(s2_prev_din);

	if (!s2_prev_valid) {
		onoff_rise = false;
		reset_rise = false;
		s2_prev_valid = true;
	}

	/* 子模式（pj4）：solo 置位 = solo，否则 classic */
	const bool solo = (din & BIT(DIN_SOLO_SYSTEM_CONFIG)) != 0U;

	if (solo != s2_solo) {
		s2_solo = solo;
		printk("S2: mode -> %s\n", solo ? "solo" : "classic");
		s2Output(s2_state, din);      /* 模式指示灯立即更新 */
	}

	/* ---- 复位优先：ME_BOX_ERROR && SYSTEM_RESET 上升沿（同 S1 规则） ---- */
	if (reset_rise && mains) {
		s2EnterState(SM_S2_RESET, din);
		s2_prev_din = din;
		return s2_state;
	}

	if (s2_state == SM_S2_RESET) {
		if (!reset && (k_uptime_get() - s2_entry_ms) >= S2_RESET_HOLD_MS) {
			s2EnterState(mains ? SM_S2_STANDBY : SM_S2_OFF_NO_MAINS, din);
		}
		s2_prev_din = din;
		return s2_state;
	}

	switch (s2_state) {

	case SM_S2_RUN:
		/* 关机请求 → 按市电在场与否进待机 / 非待机
		 * （Excel 未定义开机态市电掉电的行为 → 保持开机，同 S1 的 OR 思路） */
		if (onoff_rise) {
			if (mains) {
				s2EnterState(SM_S2_STANDBY, din);
			} else {
				s2EnterState(SM_S2_OFF_NO_MAINS, din);
			}
		} else {
			s2OutputRun(din);   /* K5/延时位随推车、IS_PC、APP_HOST 实时更新 */
		}
		break;

	case SM_S2_STANDBY:
		if (!mains) {
			s2EnterState(SM_S2_OFF_NO_MAINS, din);   /* 市电掉电 → 非待机 */
		} else if (onoff_rise) {
			s2EnterState(SM_S2_RUN, din);               /* 按键开机 */
		} else {
			s2OutputOff(din, true);
		}
		break;

	case SM_S2_OFF_NO_MAINS:
		if (mains) {
			s2EnterState(SM_S2_STANDBY, din);      /* 市电恢复 → 待机 */
		} else {
			s2OutputOff(din, false);
		}
		break;

	default:
		break;
	}

	s2_prev_din = din;
	return s2_state;
}
