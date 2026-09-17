/*!
 * @file sm_s2.c
 * @brief S2 系统状态机实现（见 sm_s2.h）
 *
 * 依据 S1_MU_SYS_ctr_logic.md：
 *   §5 "Solo with Trolley"    —— 子模式 solo  (pj4 = 1)
 *   §6 "Chassis SYS with Trolley" —— 子模式 classic (pj4 = 0)
 *
 * 状态骨架与 S1 完全同构：T0~T4 + 硬件/软件复位，判定只看市电 ME_BOX_ERROR，
 * 开关键 2s 开机/关机（按住中生效）/ 5s 软件复位（按下起算总时长）。
 *
 *   **只有 T2 判断 trolley 连接**（动态开关 LED_TROLLEY_CONNECTED /
 *   TROLLEY_ENABLE_DRV），其它状态都不考虑 trolley。
 *
 * Solo / classic 的差异：模式指示灯（s2ModeLed）与 T3(OR) 的继电器集合。
 * 管脚定义（K*、LED 宏、电平写入）来自 state_machine.h，S1/S2 共用。
 */
/*----------------------------------------------------------------------------*/
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "indicator.h"       
#include "state_machine.h"   
#include "sm_s2.h"

/* 状态名（仅本文件打印用） */
static const char *s2StateName(smS2State_t st);

/* ==================== 各子状态的继电器集合（md §5/§6） ==================== */

/* T0/T1/T4 基态：K3 + K13 待机回路 */
#define S2_STANDBY_RELAY        (K3 | K10)
#define S2_OFF_NO_MAINS_RELAY   (K3 | K13)
#define S2_SHUTDOWN_RELAY       (K3 | K10)    /* T4：K3,K10；K13 关闭 */
/* T2 系统开机：K3,K6,K7,K8_1,K8_2,K9,K10,K11,K12,K13（K4/K5 另处理）*/
#define S2_RUN_RELAY            (K3 | K6 | K7 | \
				 K8_1 | K8_2 | K9 | K10 | K11 | K12 | K13)
/* T3 开机后市电掉电（OR）：Solo 多 K5，两模式均含 K8_2 */
#define S2_RUN_OR_RELAY_SOLO    (K2 | K5 | K8_1 | K8_2 | K9 | \
				 K10 | K11 | K12 | K13)
#define S2_RUN_OR_RELAY_CHASSIS (K2 | K8_1 | K8_2 | K9 | K10 | K11 | K12 | K13)

/* ==================== 内部状态 ==================== */

static smS2State_t s2_state = SM_S2_STANDBY;
static int64_t     s2_entry_ms;
static uint32_t    s2_prev_din;
static bool        s2_prev_valid;    /* 首个 tick 不做边沿判定 */
static bool        s2_solo;          /* 子模式：pj4 */

/* 开关键（SYSTEM_ON_OFF）按下时长：与 S1 同步
 *   500ms ~ 5s（松手）→ 开机 / 关机；>= 5s（仍按住）→ 软件复位 */
#define S2_ONOFF_MIN_MS     500   /* >= 500ms：武装，松手时执行开/关机 */
#define S2_ONOFF_RESET_MS 5000   /* >= 5s：软件复位（按满后松手触发）*/

/* 软件复位：先全断，保持该时长后再进 T0/T1 */
#define S2_SW_RESET_MS 1000      /* 软件复位态保持时长，之后进 T0/T1 */

/* T2 开机后 60s 内禁止关机（只能软件复位）*/
#define S2_RUN_NO_OFF_MS 60000

static bool    s2_onoff_pressed;
static int64_t s2_onoff_start_ms;
static bool    s2_onoff_2s_fired;
static bool    s2_onoff_reset_armed;
static bool    s2_onoff_acted;

/* K4/K5 随 trolley，但按 10ms 顺序使能：0=全断, 1=K4合, 2=K4+K5合 */
#define S2_K45_STEP_MS 10
static uint8_t s2_k45_state;
static int64_t s2_k45_ms;

static bool s2_t2_rep_valid;   /* T2 小车上报缓存 */
static bool s2_t2_rep_trolley;

#define S2_RESET_HOLD_MS 2000


/* 继电器控制日志：把当前状态要合的 K 打印出来（只在集合变化时打印一次）*/
static const struct { uint64_t bit; const char *name; } s2_k_tab[] = {
	{ K2, "K2" }, { K3, "K3" }, { K4, "K4" }, { K5, "K5" }, { K6, "K6" },
	{ K7, "K7" }, { K8_1, "K8_1" }, { K8_2, "K8_2" }, { K9, "K9" },
	{ K10, "K10" }, { K11, "K11" }, { K12, "K12" }, { K13, "K13" },
};

static void s2RelayLog(const char *stage, uint64_t mask)
{
	static uint64_t prev;
	static bool     valid;
	char buf[96];
	size_t off = 0;

	if (valid && mask == prev) {
		return;   /* 集合未变 → 不打印 */
	}
	valid = true;
	prev  = mask;

	for (size_t i = 0; i < ARRAY_SIZE(s2_k_tab); i++) {
		if ((mask & s2_k_tab[i].bit) != 0U) {
			off += (size_t)snprintk(buf + off, sizeof(buf) - off, "%s ",
						s2_k_tab[i].name);
			if (off >= sizeof(buf)) {
				break;
			}
		}
	}
	if (off == 0U) {
		snprintk(buf, sizeof(buf), "(none)");
	}

	printk("S2 %s relay: %s\n", stage, buf);
}

/* ==================== 各子状态的管脚输出 ==================== */

/* 子模式指示灯：solo 亮 S2_SYS + S2_SOLO_SYS，classic 只亮 S2_SYS */
static uint64_t s2ModeLed(void)
{
	return LED_S2_SYS_ON | (s2_solo ? LED_S2_SOLO_SYS : 0ULL);
}

/* T0 上电待机（md）：K3,K13；GRID / S2_SYS(+solo) / PWR24 / CP224 */
/* 24V 判定：打印采样值（换算 mV + 原始 ADC 计数），返回是否通过 */
static bool s2Check24V(const char *stage)
{
	const uint32_t mv = sensorGetPhys(SM_24V_AIN_CH);
	const bool     ok = is24VOk();

	printk("S2: %s 24V 检测 %u mV (raw %u, 门限 %u mV) -> %s\n",
	       stage, mv, bspAinGetRawValue(SM_24V_AIN_CH), SM_24V_MIN_MV,
	       ok ? "OK" : "异常");

	return ok;
}

/* T0 待机：按 trolley 连接状态单独置/清这 4 路。
 * 注意用按位写（不是 doutWrite 全量写），否则会把 T0 其它 LED/驱动一起清掉。 */
static void s2StandbyTrolley(uint32_t din)
{
	const bool on = isTrolleyConnectedDebounced();

	bspDoutSetBitmap(LED_TROLLEY_CONNECTED, on);                              /* LED */
	bspDoutSetBitmap(DRV_TROLLEY_EN | DRV_TRL_MU_MCU | DRV_TRL_MU_IS_PC, on); /* DRV */
}

static void s2OutputStandby(uint32_t din)
{
	doutWrite(SM_RELAY_ALL, S2_STANDBY_RELAY);
	s2RelayLog("T0", S2_STANDBY_RELAY);                          /* K3 | K10 */
	doutWrite(SM_LED_ALL, s2ModeLed() | LED_GRID_PWR_IN | LED_PWR24V_ON | LED_CP24V_ON | LED_PAC230V_ON);
	doutWrite(SM_DRV_ALL, 0);
	s2StandbyTrolley(din);                /* T0 实时跟随 trolley（LED + 3 路 DRV）*/
	indicatorSetMode(INDICATOR_BREATH);   /* 待机 → 呼吸灯 */
}

/* T1 市电掉电/OR 关机（md）：保持待机回路，LED 全灭 */
static void s2OutputOffNoMains(void)
{
	// doutWrite(SM_RELAY_ALL, S2_OFF_NO_MAINS_RELAY);   /* K3 | K13 */
	// doutWrite(SM_LED_ALL, 0);
	// doutWrite(SM_DRV_ALL, 0);
	// indicatorSetMode(INDICATOR_OFF);
}

/* T2 的输出（LED + 驱动）：固定部分 + 仅 trolley 连接时点亮/使能的部分（md T2 补充）。 */
static void s2OutputRunTrolley(uint32_t din)
{
	const bool trolley = isTrolleyConnectedDebounced();

	/* T2 小车连接状态上报：只在跳变时打印（本函数每 1ms 调用一次）*/
	if (!s2_t2_rep_valid || s2_t2_rep_trolley != trolley) {
		s2_t2_rep_valid   = true;
		s2_t2_rep_trolley = trolley;
		printk("S2 T2: trolley=%d\n", trolley);
	}
	uint64_t led = s2ModeLed() | LED_SYS_ON | LED_S2_SYS_ON | LED_GRID_PWR_IN | LED_PWR24V_ON | LED_CP24V_ON | LED_PAC230V_ON;
	uint64_t drv = DRV_MAINS_CONNECTED_MCU | DRV_MAINS_CONNECTED_IS_PC |
		       DRV_IS_PC_SITE | DRV_APP_HOST;

	if (trolley) {
		led |= LED_TROLLEY_CONNECTED;
		drv |= (DRV_TROLLEY_EN | DRV_TRL_MU_MCU | DRV_TRL_MU_IS_PC);
	}
	else{
		led &= ~LED_TROLLEY_CONNECTED;
		drv &= ~(DRV_TROLLEY_EN | DRV_TRL_MU_MCU | DRV_TRL_MU_IS_PC);
	}

	doutWrite(SM_LED_ALL, led);
	doutWrite(SM_DRV_ALL, drv);

	/* K4/K5：随 trolley（连接 → K4 先合、10ms 后 K5；断开 → 立即断开），
	 * 不在 S2_RUN_RELAY 内，由本函数单独控制。 */
	if (!trolley) {
		bspDoutSetBitmap(K4 | K5, false);   /* 未连接 → 关闭 K4/K5 */
		s2_k45_state = 0U;
		return;
	}


	if (s2_k45_state == 0U) {
		bspDoutSetBitmap(K4, true);
		s2_k45_state = 1U;
		s2_k45_ms = k_uptime_get();
	} else if (s2_k45_state == 1U &&
		   (k_uptime_get() - s2_k45_ms) >= S2_K45_STEP_MS) {
		bspDoutSetBitmap(K5, true);
		s2_k45_state = 2U;
	}
}

/* RUN（md T2）系统开机：使能 S2_RUN_RELAY（K3,K6,K7,K8_1,K8_2,K9,K10,K11,K12,K13），
 * 关闭 K2 / K5 等其余；K4/K5 另由 s2OutputRunTrolley 按 trolley 顺序使能。 */
static void s2OutputRun(uint32_t din)
{
	doutWriteKeep(SM_RELAY_ALL, S2_RUN_RELAY, K4 | K5);
	s2RelayLog("T2", S2_RUN_RELAY);   /* K4/K5 不在此处动，交给 trolley 逻辑 */
	s2OutputRunTrolley(din);
	indicatorSetMode(INDICATOR_ON);   /* 开机运行 → 常亮 */
}

/* T3 开机后市电掉电（OR，md）：Solo / Chassis 继电器集合不同 */
/* T3 开机后市电掉电（OR，md）
 *   Solo    : 使能 S2_RUN_OR_RELAY_SOLO（K2,K5,K8_1,K8_2,K9…K13），关闭 K3/K6/K7 等；
 *   Chassis : 使能 S2_RUN_OR_RELAY_CHASSIS（K2,K8_1,K8_2,K9…K13），关闭 K3/K5/K6/K7 等 */
static void s2OutputRunOr(void)
{
	doutWrite(SM_RELAY_ALL, s2_solo ? S2_RUN_OR_RELAY_SOLO : S2_RUN_OR_RELAY_CHASSIS);
	s2RelayLog("T3", s2_solo ? S2_RUN_OR_RELAY_SOLO : S2_RUN_OR_RELAY_CHASSIS);
	doutWrite(SM_LED_ALL, LED_UPS_IN | s2ModeLed() | LED_SYS_ON | LED_S2_SYS_ON | LED_PWR24V_ON | LED_CP24V_ON | LED_PAC230V_ON);
	doutWrite(SM_DRV_ALL, DRV_IS_PC_SITE | DRV_APP_HOST);
	indicatorSetMode(INDICATOR_ON);
}

/* T4 正常关机（md）：K3,K13 基态 + 动态位
 *   IS_PC_ON    → K9        / DRV_IS_PC_SITE_ON
 *   APP_HOST_ON → K5, K11   / DRV_APP_HOST_SITE_ON */
/* T4 正常关机（md）：基态 K3,K13 + 动态位
 *   IS_PC_ON    → K9      / DRV_IS_PC_SITE_ON
 *   APP_HOST_ON → K5, K11 / DRV_APP_HOST_SITE_ON
 * trolley 同 T2：连接才加 LED_TROLLEY_CONNECTED + TROLLEY_EN + TRL_MU_MCU/IS_PC（不控 K4/K5）*/
/* T4 输入（trolley / IS_PC_ON / APP_HOST_ON）变化上报：
 * s2OutputShutdown 每 1ms 被调用一次，这里只在三者任一跳变时打印一行，避免刷屏。 */
/* T4 里 IS_PC / APP_HOST 输出的“锁存关断”：一旦输入变低就锁死，
 * 之后输入再变高也不恢复（重新进入 T4 时复位）。 */
static bool s2_t4_active;      /* 本次 T4 是否已做过入口重采样 */
static bool s2_t4_ispc_off;
static bool s2_t4_apphost_off;

static bool s2_t4_rep_valid;
static bool s2_t4_rep_trolley;
static bool s2_t4_rep_ispc;
static bool s2_t4_rep_apphost;

static void s2ReportShutdownInputs(uint32_t din)
{
	const bool trolley = isTrolleyConnectedDebounced();
	const bool ispc    = isPcOn(din);
	const bool apphost = isAppHostOn(din);

	if (s2_t4_rep_valid && trolley == s2_t4_rep_trolley &&
	    ispc == s2_t4_rep_ispc && apphost == s2_t4_rep_apphost) {
		return;   /* 无变化 → 不打印 */
	}
	s2_t4_rep_valid   = true;
	s2_t4_rep_trolley = trolley;
	s2_t4_rep_ispc    = ispc;
	s2_t4_rep_apphost = apphost;

	printk("S2 T4: trolley=%d is_pc_on=%d app_host_on=%d\n", trolley, ispc, apphost);
}

/* SHUTDOWN（md T4）正常关机：基态 K3,K10（K13 关闭）+ 动态位（每 1ms 随输入刷新）
 *   trolley 连接 → LED_TROLLEY_CONNECTED + TROLLEY_EN + TRL_MU_MCU/IS_PC（不控 K4/K5）
 *   IS_PC_ON    高 → K9       + DRV_IS_PC_SITE_ON；低 → 二者关闭
 *   APP_HOST_ON 高 → K5 | K11 + DRV_APP_HOST_SITE_ON；低 → 三者关闭 */
static void s2OutputShutdown(uint32_t din)
{
	const bool trolley = isTrolleyConnectedDebounced();
	uint64_t relay = S2_SHUTDOWN_RELAY;                     /* K3（K13 关闭）*/
	uint64_t led   = LED_GRID_PWR_IN | LED_PWR24V_ON | LED_CP24V_ON | LED_PAC230V_ON;
	uint64_t drv   = DRV_MAINS_CONNECTED_MCU | DRV_MAINS_CONNECTED_IS_PC;

	s2ReportShutdownInputs(din);

	if (trolley) {
		led |= LED_TROLLEY_CONNECTED;
		drv |= DRV_TROLLEY_EN | DRV_TRL_MU_MCU | DRV_TRL_MU_IS_PC;
	}
	else {
		led &= ~LED_TROLLEY_CONNECTED;
		drv &= ~(DRV_TROLLEY_EN | DRV_TRL_MU_MCU | DRV_TRL_MU_IS_PC);
	}

	/* 进入 T4 后的“第一次”调用：先把锁存清零，从此刻起重新采样这两个输入。
	 * 之前状态里出现过的低电平不会被带进来 —— 只有进入关机态之后看到的低才算。 */
	if (!s2_t4_active) {
		s2_t4_active      = true;
		s2_t4_ispc_off    = false;
		s2_t4_apphost_off = false;
	}

	/* IS_PC / APP_HOST：锁存单向 —— 输入为高则输出高；一旦（进入 T4 之后）输入变低
	 * 就锁存关断，之后再变高也不恢复（要重新进入 T4 才会复位锁存）。 */
	if (!isPcOn(din)) {
		s2_t4_ispc_off = true;
	}
	if (!isAppHostOn(din)) {
		s2_t4_apphost_off = true;
	}

	if (isPcOn(din) && !s2_t4_ispc_off) {
		relay |= K9;
		drv   |= DRV_IS_PC_SITE;
	}
	else {
		relay &= ~K9;               /* IS_PC 侧关闭（输入低 / 已锁存关断）*/
		drv   &= ~DRV_IS_PC_SITE;
	}

	if (isAppHostOn(din) && !s2_t4_apphost_off) {
		relay |= (K5 | K11);
		drv   |= DRV_APP_HOST;
	}
	else {
		relay &= ~(K5 | K11);       /* APP_HOST 侧关闭（输入低 / 已锁存关断）*/
		drv   &= ~DRV_APP_HOST;
	}

	doutWrite(SM_RELAY_ALL, relay);
	s2RelayLog("T4", relay);
	doutWrite(SM_LED_ALL, led);
	doutWrite(SM_DRV_ALL, drv);
	indicatorSetMode(INDICATOR_BREATH);   /* 关机态与待机一致：呼吸灯 */
}

/* RESET 硬件复位：全部断开 */
/* 软件复位（md 软件复位）：先把 K3/K10 及其余全部输出关闭 */
static void s2OutputSwReset(void)
{
	doutWrite(SM_RELAY_ALL, 0);   /* K3/K10/K13 … 全断 */
	s2RelayLog("SW_RESET", 0);
	doutWrite(SM_LED_ALL, 0);
	doutWrite(SM_DRV_ALL, 0);
	indicatorSetMode(INDICATOR_OFF);
}

static void s2OutputReset(void)
{
	doutWrite(SM_RELAY_ALL, 0);
	s2RelayLog("RESET", 0);
	doutWrite(SM_LED_ALL, 0);
	doutWrite(SM_DRV_ALL, 0);
	indicatorSetMode(INDICATOR_OFF);
}

/* 进入某子状态时写它自己的输出（每 1ms 也会由 smS2Tick 再调一次，需幂等）*/
static void s2Output(smS2State_t st, uint32_t din)
{
	switch (st) {
	case SM_S2_STANDBY:      s2OutputStandby(din);   break;
	case SM_S2_OFF_NO_MAINS: s2OutputOffNoMains();   break;
	case SM_S2_RUN:          s2OutputRun(din);       break;
	case SM_S2_RUN_OR:       s2OutputRunOr();        break;
	case SM_S2_SHUTDOWN:     s2OutputShutdown(din);  break;
	case SM_S2_RESET:        s2OutputReset();        break;
	case SM_S2_SW_RESET:     s2OutputSwReset();     break;
	default:                                         break;
	}
}

/* ==================== 状态切换 ==================== */

static void s2EnterState(smS2State_t st, uint32_t din)
{
	s2_state    = st;
	s2_entry_ms = k_uptime_get();
	s2_k45_state      = 0U;   /* K4/K5 顺序使能重新开始 */

	printk("S2: -> %s\n", s2StateName(st));

	s2_t2_rep_valid = false;   /* 新状态：T2 小车上报缓存失效 */
	s2_t4_rep_valid = false;
	s2_t4_active = false;     /* 新状态：下次进 T4 会重新采样 IS_PC/APP_HOST */

	s2Output(st, din);
}

/* ==================== 对外接口 ==================== */

static const char *s2StateName(smS2State_t st)
{
	switch (st) {
	case SM_S2_STANDBY:      return "STANDBY 上电待机 (T0)";
	case SM_S2_OFF_NO_MAINS: return "OFF 市电掉电/OR关机 (T1)";
	case SM_S2_RUN:          return "RUN 系统开机 (T2)";
	case SM_S2_RUN_OR:       return "RUN_OR 开机后市电掉电OR (T3)";
	case SM_S2_SHUTDOWN:     return "SHUTDOWN 正常关机 (T4)";
	case SM_S2_RESET:        return "RESET 硬件复位";
	case SM_S2_SW_RESET:     return "SW_RESET 软件复位";
	default:                 return "?";
	}
}

void smS2Enter(void)
{
	const uint32_t din = bspDinGetBitmap();

	s2_prev_valid = false;
	s2_prev_din   = 0;
	s2_onoff_pressed    = false;
	s2_onoff_start_ms   = 0;
	s2_onoff_2s_fired   = false;
	s2_onoff_reset_armed = false;
	s2_onoff_acted      = false;

	/* 初始化子模式（pj4）并打印一次：solo / classic 开机都有一条 */
	s2_solo = (din & BIT(DIN_SOLO_SYSTEM_CONFIG)) != 0U;
	printk("S2: mode -> %s\n", s2_solo ? "solo" : "classic");

	/* 按当前输入直接进入正确初始态：市电正常 → T0，掉电 → T1 */
	s2EnterState(isMainsOk(din) ? SM_S2_STANDBY : SM_S2_OFF_NO_MAINS, din);
}

smS2State_t smS2Tick(uint32_t din)
{
	const int64_t now        = k_uptime_get();
	const bool    mains      = isMainsOk(din);
	const bool    reset      = isResetActive(din);
	const bool    onoff      = isOnOffActive(din);
	const bool    onoff_prev = isOnOffActive(s2_prev_din);

	bool reset_rise  = reset && !isResetActive(s2_prev_din);
	bool onoff_2s    = false;   /* 满 500ms 后松手：开机 / 关机 */
	bool onoff_long  = false;   /* 按满 5s（仍按住）：软件复位 */

	/* ---- 开关键计时（低→高 = 按下）----
	 *   按满 500ms 后「松手」(高→低) → onoff_2s  （开机/关机，每次按下只一次）；
	 *   按满 5s    后「松手」(高→低) → onoff_long（软件复位）。
	 *   两者都在松手沿触发；>= 5s 时只复位、不再执行开/关机。 */
	if (!s2_prev_valid) {
		reset_rise = false;
		s2_onoff_pressed    = onoff;
		s2_onoff_start_ms   = onoff ? now : 0;
		s2_onoff_2s_fired   = false;
		s2_onoff_reset_armed = false;
		s2_onoff_acted      = false;
		s2_prev_valid = true;
	} else if (onoff && !onoff_prev) {              /* 低→高：按下 */
		s2_onoff_pressed    = true;
		s2_onoff_start_ms   = now;
		s2_onoff_2s_fired   = false;
		s2_onoff_reset_armed = false;
		s2_onoff_acted      = false;
	} else if (!onoff && onoff_prev) {              /* 高→低：松手 → 在此刻动作 */
		s2_onoff_pressed = false;
		if (s2_onoff_reset_armed) {
			onoff_long = true;                      /* 满 5s 后松手 → 软件复位 */
		} else if (s2_onoff_2s_fired) {
			onoff_2s = true;                        /* 满 500ms 后松手 → 开机/关机 */
		}
		s2_onoff_2s_fired    = false;
		s2_onoff_reset_armed = false;
		s2_onoff_acted       = false;
	}

	if (onoff && s2_onoff_pressed) {
		const int64_t held = now - s2_onoff_start_ms;

		if (held >= S2_ONOFF_RESET_MS) {
			s2_onoff_reset_armed = true;            /* 满 5s：复位武装 */
		} else if (held >= S2_ONOFF_MIN_MS) {
			s2_onoff_2s_fired = true;               /* 满 500ms：开/关机武装 */
		}
	}

	/* ---- 子模式（pj4）：solo 置位 = solo，否则 classic ---- */
	const bool solo = (din & BIT(DIN_SOLO_SYSTEM_CONFIG)) != 0U;

	if (solo != s2_solo) {
		s2_solo = solo;
		printk("S2: mode -> %s\n", solo ? "solo" : "classic");
		s2Output(s2_state, din);      /* 模式指示灯 / T3 继电器立即更新 */
	}

	/* ---- 1) 硬件复位优先：SYSTEM_RESET 上升沿 → 全部重新初始化 ---- */
	if (reset_rise) {
		s2_onoff_pressed    = false;
		s2_onoff_2s_fired   = false;
		s2_onoff_reset_armed = false;
		s2_onoff_acted      = false;
		s2EnterState(SM_S2_RESET, din);
		s2_prev_din = din;
		return s2_state;
	}

	/* ---- 2) 硬件复位态：reset 释放后 2s，市电正常回 T0，否则保持关机 ---- */
	if (s2_state == SM_S2_RESET) {
		if (!reset && (now - s2_entry_ms) >= S2_RESET_HOLD_MS) {
			s2EnterState(mains ? SM_S2_STANDBY : SM_S2_OFF_NO_MAINS, din);
		}
		s2_prev_din = din;
		return s2_state;
	}

	/* ---- 3) 软件复位：SYSTEM_ON_OFF 按满 >= 5s 后松手（任意状态）----
	 * 进入独立的 SW_RESET 子状态：先把 K3/K10 等全部输出关闭，稳定后再进 T0/T1。 */
	if (onoff_long) {
		s2EnterState(SM_S2_SW_RESET, din);
		s2_prev_din = din;
		return s2_state;
	}

	/* ---- 4) 软件复位态：输出已全断，等待 SW_RESET_MS 后按 Tx 判定进 T0/T1 ---- */
	if (s2_state == SM_S2_SW_RESET) {
		if ((now - s2_entry_ms) >= S2_SW_RESET_MS) {
			s2EnterState(mains ? SM_S2_STANDBY : SM_S2_OFF_NO_MAINS, din);
		}
		s2_prev_din = din;
		return s2_state;
	}


	/* ---- 状态转换（只看市电）---- */
	switch (s2_state) {

	case SM_S2_STANDBY:   /* T0 上电待机 */
		if (!mains) {
			s2EnterState(SM_S2_OFF_NO_MAINS, din);
		} else if (onoff_2s && s2Check24V("开机")) {
			s2_onoff_acted = true;              /* 本次按下已动作 → 不再关机 */
			s2EnterState(SM_S2_RUN, din);       /* 满 500ms 松手且 24V 正常 → 开机 */
		}
		break;

	case SM_S2_OFF_NO_MAINS:   /* T1 市电掉电 / OR 关机 */
		if (mains) {
			s2EnterState(SM_S2_STANDBY, din);
		}
		break;

	case SM_S2_RUN:   /* T2 系统开机 */
		if (!mains) {
			s2EnterState(SM_S2_RUN_OR, din);    /* 开机后市电掉电 → OR(T3) */
		} else if (onoff_2s && !s2_onoff_acted && s2Check24V("关机")) {
			s2_onoff_acted = true;              /* 本次按下已动作 */
			s2EnterState(SM_S2_SHUTDOWN, din);  /* 满 500ms 松手且 24V 正常 → 正常关机 */
		}
		break;

	case SM_S2_RUN_OR:   /* T3 开机后市电掉电（OR 模式） */
		if (mains) {
			s2EnterState(SM_S2_RUN, din);
		}
		else if (onoff_2s && !s2_onoff_acted && s2Check24V("关机")) {
			s2_onoff_acted = true;                   /* 本次按下已动作 */
			s2EnterState(SM_S2_SHUTDOWN, din);       /* OR 态下再按 → 正常关机(T4) */
		}
		break;

	case SM_S2_SHUTDOWN:   /* T4 正常关机（稳态：保持关机输出，等按键重新开机）*/
		if (!mains) {
			s2EnterState(SM_S2_OFF_NO_MAINS, din);   /* 市电掉电 → T1 */
		} else if (onoff_2s && !s2_onoff_acted && s2Check24V("开机")) {
			s2_onoff_acted = true;
			s2EnterState(SM_S2_RUN, din);            /* 再按（500ms~5s 松手）→ T2 */
		}
		break;

	default:
		break;
	}

	/* 每 1ms 重新应用当前状态的输出（幂等）：solo / trolley 跟随 / K4,K5 推进 /
	 * T4 的 IS_PC、APP_HOST 位都随之刷新。 */
	s2Output(s2_state, din);

	/* 开关键按下期间：指示灯走双倍频率呼吸 */
	indicatorSetBreathFast(onoff);

	/* 全局指示灯：PD10 = S2 系统（S2 模式内常亮）；PD5 = S2 solo；PD6 = 推车连接 */
	bspDoutSetBitmap(BIT64(DOUT_LED_S2_SYS_ON), true);
	bspDoutSetBitmap(BIT64(DOUT_LED_S2_SOLO_SYS), s2_solo);
	bspDoutSetBitmap(BIT64(DOUT_LED_TROLLEY_CONNECTED), isTrolleyConnectedDebounced());

	s2_prev_din = din;
	return s2_state;
}
