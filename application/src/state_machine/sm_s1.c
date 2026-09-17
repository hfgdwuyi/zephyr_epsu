/*!
 * @file sm_s1.c
 * @brief S1 系统状态机实现（见 sm_s1.h）
 *
 * 管脚处理：直接操作 BSP 的位图接口 bspDoutSetBitmap()，在各自子状态中
 * 指定位置调用 —— 没有中间状态表，也没有通用 apply 包装：
 *
 *   doutWrite(SM_RELAY_ALL, S1_STANDBY_RELAY);                // 继电器电平：要的写 1，其余写 0
 *   doutWrite(SM_LED_ALL, ...);                               // 面板 LED：要的写 1，其余 LED 写 0
 *   doutWrite(SM_DRV_ALL, ...);                             // 状态驱动 (DRV_*)：要的写 1，其余写 0
 *
 * 每个状态写出自己那一份完整电平，不做"整组复位再重设"：
 * 本状态仍然要合的路（例如 RUN 状态里的 K3/K13）不会被中途拉掉，
 * 只把本状态不需要的路明确写 0。relay 与 led 写法完全一致，不区分系统归属。
 * K2~K7 是交流继电器，K8_1/K8_2/K9~K13 是 efuse。每个状态用 doutWrite(SM_RELAY_ALL, 集合)
 * 一次性给出该状态的完整继电器电平：集合内的合上，其余（含上一状态遗留的）全部写 0。
 * 例：T2 使能 S1_RUN_RELAY，K2 等其余关闭；T3 使能 S1_RUN_OR_RELAY，K3/K6 等关闭。
 *
 * WDI(PH9) 归 max6703a 模块，本文件一律不触碰。
 */
/*----------------------------------------------------------------------------*/
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "indicator.h"       /* 状态指示灯（PA5，应用层） */
#include "state_machine.h"   /* 管脚定义 / 电平写入 / 输入判定（S1/S2 共用） */
#include "sm_s1.h"

/* 状态名（仅本文件打印用） */
static const char *s1StateName(smS1State_t st);

/* ==================== 各子状态的继电器集合 ====================
 * 每个状态要合哪几路继电器，在这里显式写出来（与 md 的 Relay sequence 对应）：
 *   K2~K7            = 交流继电器（AC）
 *   K8_1/K8_2/K9~K13 = efuse
 */
/* 待机回路（md T0） */
#define S1_STANDBY_RELAY      (K3 | K10)
/* 市电掉电仍保持待机回路（md T1） */
#define S1_OFF_NO_MAINS_RELAY (K3 | K13)
/* 开机（md T2）：AC 与 efuse 两轨同时起步、各轨内部 10ms 依次延时。
 * 注：T2 额外加入 K10 使能；K4/K5 不在集合内，由 s1OutputRunTrolley 按 trolley 连接动态控制。 */
#define S1_RUN_RELAY          (K3 | K6 | K7 | \
			       K8_1 | K9 | K10 | K11 | K12 | K13)
/* OR 模式（md T3）：市电掉电、推车供电。注意 S1 表 T3 无 K6。 */
#define S1_RUN_OR_RELAY       (K2 | K7 | \
			       K8_1 | K9 | K10 | K11 | K12 | K13)
/* 正常关机（md T4）：待机回路 + K9/K11（IS_PC_ON）/ K10（APP_HOST_ON）动态决定 */
#define S1_SHUTDOWN_RELAY     (K3 | K10)    /* T4：K3,K10；K13 关闭 */

/* ==================== 内部状态 ==================== */

static smS1State_t s1_state = SM_S1_STANDBY;
static int64_t     s1_entry_ms;
static uint32_t    s1_prev_din;
static bool        s1_prev_valid;   /* 首个 tick 不做边沿判定（无历史值） */

/* 开关键（SYSTEM_ON_OFF1）按下时长门限（ms）：
 *   500ms ~ 5s（松手）→ onoff_2s  ：开机 / 关机（按满 500ms 后松手瞬间生效）
 *   >= 5s（仍按住）   → onoff_long：软件复位（从按下时刻起算，不受 500ms 影响）
 *   同一次按下只执行一次开/关机（acted）；关机后 T4 为稳态，再按一次开机。 */
#define S1_ONOFF_MIN_MS     500   /* >= 500ms：武装，松手时执行开/关机 */
#define S1_ONOFF_RESET_MS 5000   /* >= 5s：软件复位（按满后松手触发）*/

/* 软件复位：先全断，保持该时长后再进 T0/T1 */
#define S1_SW_RESET_MS 1000      /* 软件复位态保持时长，之后进 T0/T1 */

/* 硬件复位：SYSTEM_RESET 释放后再保持该时长，市电正常才回 T0 */
#define S1_RESET_HOLD_MS 2000

/* T2 开机后 60s 内禁止关机（只能软件复位）*/
#define S1_RUN_NO_OFF_MS 60000

static bool    s1_onoff_pressed;      /* 当前是否处于按下过程 */
static int64_t s1_onoff_start_ms;     /* 本次按下的起始时刻 */
static bool    s1_onoff_2s_fired;     /* 已按满 500ms（武装，松手时执行开/关机）*/
static bool    s1_onoff_reset_armed;  /* 本次按下是否已按满 5s（松手时复位）*/
static bool    s1_onoff_acted;        /* 本次按下是否已执行过开/关机（松手事件期）*/

/* K4/K5 随 trolley，但按 10ms 顺序使能：0=全断, 1=K4合, 2=K4+K5合 */
#define S1_K45_STEP_MS 10
static uint8_t s1_k45_state;
static int64_t s1_k45_ms;

static bool s1_t2_rep_valid;   /* T2 小车上报缓存 */
static bool s1_t2_rep_trolley;

/* 继电器控制日志：把当前状态要合的 K 打印出来（只在集合变化时打印一次）*/
static const struct { uint64_t bit; const char *name; } s1_k_tab[] = {
	{ K2, "K2" }, { K3, "K3" }, { K4, "K4" }, { K5, "K5" }, { K6, "K6" },
	{ K7, "K7" }, { K8_1, "K8_1" }, { K8_2, "K8_2" }, { K9, "K9" },
	{ K10, "K10" }, { K11, "K11" }, { K12, "K12" }, { K13, "K13" },
};

static void s1RelayLog(const char *stage, uint64_t mask)
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

	for (size_t i = 0; i < ARRAY_SIZE(s1_k_tab); i++) {
		if ((mask & s1_k_tab[i].bit) != 0U) {
			off += (size_t)snprintk(buf + off, sizeof(buf) - off, "%s ",
						s1_k_tab[i].name);
			if (off >= sizeof(buf)) {
				break;
			}
		}
	}
	if (off == 0U) {
		snprintk(buf, sizeof(buf), "(none)");
	}

	printk("S1 %s relay: %s\n", stage, buf);
}

/* ==================== 各子状态的管脚输出 ==================== */
/* 管脚号直接写在各状态自己的函数里，改哪一路就改哪一行 */

/* 24V 判定：打印采样值（换算 mV + 原始 ADC 计数），返回是否通过 */
static bool s1Check24V(const char *stage)
{
	const uint32_t mv = sensorGetPhys(SM_24V_AIN_CH);
	const bool     ok = is24VOk();

	printk("S1: %s 24V 检测 %u mV (raw %u, 门限 %u mV) -> %s\n",
	       stage, mv, bspAinGetRawValue(SM_24V_AIN_CH), SM_24V_MIN_MV,
	       ok ? "OK" : "异常");

	return ok;
}

/* T0 待机：按 trolley 连接状态单独置/清这 4 路。
 * 注意用按位写（不是 doutWrite 全量写），否则会把 T0 其它 LED/驱动一起清掉。 */
static void s1StandbyTrolley(uint32_t din)
{
	const bool on = isTrolleyConnectedDebounced();

	bspDoutSetBitmap(LED_TROLLEY_CONNECTED, on);                              /* LED */
	bspDoutSetBitmap(DRV_TROLLEY_EN | DRV_TRL_MU_MCU | DRV_TRL_MU_IS_PC, on); /* DRV */
}

/* STANDBY（md T0）上电待机：K3 + K13 待机回路 + 供电指示 */
static void s1OutputStandby(uint32_t din)
{
	doutWrite(SM_RELAY_ALL, S1_STANDBY_RELAY);
	s1RelayLog("T0", S1_STANDBY_RELAY);
	/* md T0 输出：GRID / S1_SYS_ON / PWR24 / CP224 / PAC230V + MAINS_CONNECTED_*
	 * （PD4 LED_SYS_ON 仅开机态使能，待机不亮）*/
	doutWrite(SM_LED_ALL, LED_GRID_PWR_IN | LED_S1_SYS_ON | LED_PWR24V_ON | LED_CP24V_ON | LED_PAC230V_ON);
	doutWrite(SM_DRV_ALL, 0);   /* T0：K9/K5/K11 由继电器组清；DRV_IS_PC_SITE / DRV_APP_HOST 在这里清 */
	s1StandbyTrolley(din);   /* T0 也实时跟随 trolley（LED + DRV）*/
	/* 状态指示灯：待机 → 呼吸灯 */
	indicatorSetMode(INDICATOR_BREATH);
}

/* OFF_NO_MAINS（md T1）市电掉电 / OR 关机：继电器保持待机回路（同 STANDBY）
 * LED：md 未给出该状态的点亮要求 → 全灭（若需与 STANDBY 一致请说明） */
/* T1 市电掉电 / OR 关机（md）：保持待机回路（K3,K13），LED / 驱动全灭 */
static void s1OutputOffNoMains(void)
{
	// doutWrite(SM_RELAY_ALL, S1_OFF_NO_MAINS_RELAY);   /* K3 | K13 */
	// doutWrite(SM_LED_ALL, 0);
	// doutWrite(SM_DRV_ALL, 0);
	// indicatorSetMode(INDICATOR_OFF);
}

/* T2 的输出（LED + 驱动）：固定部分 + 仅推车连接时点亮/使能的部分（md T2 补充：
 * 连接 → LED_TROLLEY_CONNECTED / TROLLEY_ENABLE_DRV / TRL_MU_CONNECTED_MCU /
 * TRL_MU_CONNECTED_IS_PC；断开 → 关断）。每个 tick 调用以持续跟随 trolley。 */
static void s1OutputRunTrolley(uint32_t din)
{
	const bool trolley = isTrolleyConnectedDebounced();

	/* T2 小车连接状态上报：只在跳变时打印（本函数每 1ms 调用一次）*/
	if (!s1_t2_rep_valid || s1_t2_rep_trolley != trolley) {
		s1_t2_rep_valid   = true;
		s1_t2_rep_trolley = trolley;
		printk("S1 T2: trolley=%d\n", trolley);
	}
	uint64_t led = LED_GRID_PWR_IN | LED_S1_SYS_ON | LED_SYS_ON | LED_PWR24V_ON | LED_CP24V_ON | LED_PAC230V_ON;
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
	 * 不在 S1_RUN_RELAY 内，由本函数单独控制。 */
	if (!trolley) {
		bspDoutSetBitmap(K4 | K5, false);   /* 未连接 → 关闭 K4/K5 */
		s1_k45_state = 0U;
		return;
	}


	if (s1_k45_state == 0U) {
		bspDoutSetBitmap(K4, true);
		s1_k45_state = 1U;
		s1_k45_ms = k_uptime_get();
	} else if (s1_k45_state == 1U &&
		   (k_uptime_get() - s1_k45_ms) >= S1_K45_STEP_MS) {
		bspDoutSetBitmap(K5, true);
		s1_k45_state = 2U;
	}
}

/* RUN（md T2）系统开机：使能 S1_RUN_RELAY（K3,K6,K7,K8_1,K9,K10,K11,K12,K13），
 * 关闭 K2 / K4 / K5 等其余；K4/K5 另由 s1OutputRunTrolley 按 trolley 顺序使能。 */
static void s1OutputRun(uint32_t din)
{
	doutWriteKeep(SM_RELAY_ALL, S1_RUN_RELAY, K4 | K5);
	s1RelayLog("T2", S1_RUN_RELAY);   /* K4/K5 不在此处动，交给 trolley 逻辑 */
	s1OutputRunTrolley(din);
	indicatorSetMode(INDICATOR_ON);   /* 开机运行 → 常亮 */
}

/* RUN_OR（md T3）开机后市电掉电（OR 模式）：使能 S1_RUN_OR_RELAY
 * （K2,K7,K8_1,K9,K10,K11,K12,K13），关闭 K3 / K6 等其余。 */
static void s1OutputRunOr(void)
{
	/* OR 态市电掉电：LED_GRID_PWR_IN 不在 mask 内 → doutWrite(SM_LED_ALL, …) 会自动熄灭它 */
	doutWrite(SM_LED_ALL, LED_UPS_IN | LED_S1_SYS_ON | LED_SYS_ON | LED_PWR24V_ON | LED_CP24V_ON |
		 LED_PAC230V_ON | LED_TROLLEY_CONNECTED);
	doutWrite(SM_DRV_ALL, DRV_IS_PC_SITE | DRV_APP_HOST);
	indicatorSetMode(INDICATOR_ON);   /* 开机运行(OR) → 常亮 */
	doutWrite(SM_RELAY_ALL, S1_RUN_OR_RELAY);
	s1RelayLog("T3", S1_RUN_OR_RELAY);
}

/* T4 输入（trolley / IS_PC_ON / APP_HOST_ON）变化上报：
 * s1OutputShutdown 每 1ms 被调用一次，这里只在三者任一跳变时打印一行，避免刷屏。 */
/* T4 里 IS_PC / APP_HOST 输出的“锁存关断”：一旦输入变低就锁死，
 * 之后输入再变高也不恢复（重新进入 T4 时复位）。 */
static bool s1_t4_active;      /* 本次 T4 是否已做过入口重采样 */
static bool s1_t4_ispc_off;
static bool s1_t4_apphost_off;

static bool s1_t4_rep_valid;
static bool s1_t4_rep_trolley;
static bool s1_t4_rep_ispc;
static bool s1_t4_rep_apphost;

static void s1ReportShutdownInputs(uint32_t din)
{
	const bool trolley = isTrolleyConnectedDebounced();
	const bool ispc    = isPcOn(din);
	const bool apphost = isAppHostOn(din);

	if (s1_t4_rep_valid && trolley == s1_t4_rep_trolley &&
	    ispc == s1_t4_rep_ispc && apphost == s1_t4_rep_apphost) {
		return;   /* 无变化 → 不打印 */
	}
	s1_t4_rep_valid   = true;
	s1_t4_rep_trolley = trolley;
	s1_t4_rep_ispc    = ispc;
	s1_t4_rep_apphost = apphost;

	printk("S1 T4: trolley=%d is_pc_on=%d app_host_on=%d\n", trolley, ispc, apphost);
}

/* SHUTDOWN（md T4）正常关机：基态 K3,K10（K13 关闭）+ 动态位（每 1ms 随输入刷新）
 *   trolley 连接 → LED_TROLLEY_CONNECTED + TROLLEY_EN + TRL_MU_MCU/IS_PC（不控 K4/K5）
 *   IS_PC_ON    高 → K9 | K11 + DRV_IS_PC_SITE_ON；低 → 三者关闭
 *   APP_HOST_ON 高 → K13      + DRV_APP_HOST_SITE_ON；低 → 二者关闭 */
static void s1OutputShutdown(uint32_t din)
{
	const bool trolley = isTrolleyConnectedDebounced();
	uint64_t relay = S1_SHUTDOWN_RELAY;                     /* K3 | K10（K13 关闭）*/
	uint64_t led   = LED_GRID_PWR_IN | LED_PWR24V_ON | LED_CP24V_ON | LED_PAC230V_ON;
	uint64_t drv   = DRV_MAINS_CONNECTED_MCU | DRV_MAINS_CONNECTED_IS_PC;

	s1ReportShutdownInputs(din);

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
	if (!s1_t4_active) {
		s1_t4_active      = true;
		s1_t4_ispc_off    = false;
		s1_t4_apphost_off = false;
	}

	/* IS_PC / APP_HOST：锁存单向 —— 输入为高则输出高；一旦（进入 T4 之后）输入变低
	 * 就锁存关断，之后再变高也不恢复（要重新进入 T4 才会复位锁存）。 */
	if (!isPcOn(din)) {
		s1_t4_ispc_off = true;
	}
	if (!isAppHostOn(din)) {
		s1_t4_apphost_off = true;
	}

	if (isPcOn(din) && !s1_t4_ispc_off) {
		relay |= (K9 | K11);
		drv   |= DRV_IS_PC_SITE;
	}
	else {
		relay &= ~(K9 | K11);               /* IS_PC 侧关闭（输入低 / 已锁存关断）*/
		drv   &= ~DRV_IS_PC_SITE;
	}

	if (isAppHostOn(din) && !s1_t4_apphost_off) {
		relay |= K13;
		drv   |= DRV_APP_HOST;
	}
	else {
		relay &= ~K13;               /* APP_HOST 侧关闭（输入低 / 已锁存关断）*/
		drv   &= ~DRV_APP_HOST;
	}

	doutWrite(SM_RELAY_ALL, relay);
	s1RelayLog("T4", relay);
	doutWrite(SM_LED_ALL, led);
	doutWrite(SM_DRV_ALL, drv);
	indicatorSetMode(INDICATOR_BREATH);   /* 关机态与待机一致：呼吸灯 */
}

/* RESET 硬件复位：全部断开（md：K OFF）*/
/* 软件复位（md 软件复位）：先把 K3/K10 及其余全部输出关闭 */
static void s1OutputSwReset(void)
{
	doutWrite(SM_RELAY_ALL, 0);   /* K3/K10/K13 … 全断 */
	s1RelayLog("SW_RESET", 0);
	doutWrite(SM_LED_ALL, 0);
	doutWrite(SM_DRV_ALL, 0);
	indicatorSetMode(INDICATOR_OFF);
}

static void s1OutputReset(void)
{
	doutWrite(SM_RELAY_ALL, 0);
	s1RelayLog("RESET", 0);
	doutWrite(SM_LED_ALL, 0);
	doutWrite(SM_DRV_ALL, 0);
	indicatorSetMode(INDICATOR_OFF);
}

/* 进入某子状态时写它自己的输出（每 1ms 也会由 smS1Tick 再调一次，需幂等）*/
static void s1Output(smS1State_t st, uint32_t din)
{
	switch (st) {
	case SM_S1_STANDBY:      s1OutputStandby(din);   break;
	case SM_S1_OFF_NO_MAINS: s1OutputOffNoMains();   break;
	case SM_S1_RUN:          s1OutputRun(din);       break;
	case SM_S1_RUN_OR:       s1OutputRunOr();        break;
	case SM_S1_SHUTDOWN:     s1OutputShutdown(din);  break;
	case SM_S1_RESET:        s1OutputReset();        break;
	case SM_S1_SW_RESET:     s1OutputSwReset();     break;
	default:                 						 break;
	}
}

/* ==================== 状态切换 ==================== */

static void s1EnterState(smS1State_t st, uint32_t din)
{
	s1_state    = st;
	s1_entry_ms = k_uptime_get();
	s1_k45_state      = 0U;   /* K4/K5 顺序使能重新开始 */

	printk("S1: -> %s\n", s1StateName(st));

	s1_t2_rep_valid = false;   /* 新状态：T2 小车上报缓存失效 */
	s1_t4_rep_valid = false;
	s1_t4_active = false;     /* 新状态：下次进 T4 会重新采样 IS_PC/APP_HOST */

	/* 进入时立即写一次：RESET 态在 tick 里会提前 return，到不了每 tick 的
	 * s1Output；其它态只是提前 1ms 生效（幂等，无副作用）。 */
	s1Output(st, din);
}

/* ==================== 对外接口 ==================== */

static const char *s1StateName(smS1State_t st)
{
	switch (st) {
	case SM_S1_STANDBY:      return "STANDBY 上电待机 (T0)";
	case SM_S1_OFF_NO_MAINS: return "OFF 市电掉电/OR关机 (T1)";
	case SM_S1_RUN:          return "RUN 系统开机 (T2)";
	case SM_S1_RUN_OR:       return "RUN 开机后市电掉电OR (T3)";
	case SM_S1_SHUTDOWN:     return "SHUTDOWN 正常关机 (T4)";
	case SM_S1_RESET:        return "RESET 硬件复位";
	case SM_S1_SW_RESET:     return "SW_RESET 软件复位";
	default:                 return "?";
	}
}

void smS1Enter(void)
{
	s1_prev_valid = false;      /* 进入后首个 tick 只记录输入，不判边沿 */
	s1_prev_din   = 0;
	s1_onoff_pressed    = false;
	s1_onoff_start_ms   = 0;
	s1_onoff_2s_fired   = false;
	s1_onoff_reset_armed = false;
	s1_onoff_acted      = false;

	/* 按当前输入直接进入正确的初始子状态：
	 * 市电正常 → T0(STANDBY)，市电掉电 → T1(OFF_NO_MAINS)，
	 * 避免先误进 T0 再被下一个 tick 拉去 T1。 */
	const uint32_t din = bspDinGetBitmap();

	s1EnterState(isMainsOk(din) ? SM_S1_STANDBY : SM_S1_OFF_NO_MAINS, din);
}

smS1State_t smS1Tick(uint32_t din)
{
	const int64_t now        = k_uptime_get();
	const bool    mains      = isMainsOk(din);
	const bool    onoff      = isOnOffActive(din);
	const bool    onoff_prev = isOnOffActive(s1_prev_din);
	const bool    reset      = isResetActive(din);
	const bool    reset_prev = isResetActive(s1_prev_din);

	bool reset_rise  = reset && !reset_prev;
	bool onoff_2s    = false;   /* 满 500ms 后松手：开机 / 关机 */
	bool onoff_long  = false;   /* 按满 5s（仍按住）：软件复位 */

	/* ---- 开关键计时（低→高 = 按下）----
	 *   按满 500ms 后「松手」(高→低) → onoff_2s  （开机/关机，每次按下只一次）；
	 *   按满 5s    后「松手」(高→低) → onoff_long（软件复位）。
	 *   两者都在松手沿触发；>= 5s 时只复位、不再执行开/关机。 */
	if (!s1_prev_valid) {
		reset_rise = false;
		s1_onoff_pressed    = onoff;
		s1_onoff_start_ms   = onoff ? now : 0;
		s1_onoff_2s_fired   = false;
		s1_onoff_reset_armed = false;
		s1_onoff_acted      = false;
		s1_prev_valid = true;
	} else if (onoff && !onoff_prev) {              /* 低→高：按下 */
		s1_onoff_pressed    = true;
		s1_onoff_start_ms   = now;
		s1_onoff_2s_fired   = false;
		s1_onoff_reset_armed = false;
		s1_onoff_acted      = false;
	} else if (!onoff && onoff_prev) {              /* 高→低：松手 → 在此刻动作 */
		s1_onoff_pressed = false;
		if (s1_onoff_reset_armed) {
			onoff_long = true;                      /* 满 5s 后松手 → 软件复位 */
		} else if (s1_onoff_2s_fired) {
			onoff_2s = true;                        /* 满 500ms 后松手 → 开机/关机 */
		}
		s1_onoff_2s_fired    = false;
		s1_onoff_reset_armed = false;
		s1_onoff_acted       = false;
	}

	if (onoff && s1_onoff_pressed) {
		const int64_t held = now - s1_onoff_start_ms;

		if (held >= S1_ONOFF_RESET_MS) {
			s1_onoff_reset_armed = true;            /* 满 5s：复位武装 */
		} else if (held >= S1_ONOFF_MIN_MS) {
			s1_onoff_2s_fired = true;               /* 满 500ms：开/关机武装 */
		}
	}

	/* 判定规则（当前版本）：全部只看市电 ME_BOX_ERROR。
	 *   - T1(市电掉电) 只由 mains == false 触发；
	 *   - 开机中市电掉电 → T3(OR)；
	 *   - 暂不判断 trolley 连接状态（不参与任何状态迁移）。 */

	/* ---- 1) 硬件复位优先：SYSTEM_RESET 上升沿 → 全部重新初始化 ---- */
	if (reset_rise) {
		s1_onoff_pressed    = false;
		s1_onoff_2s_fired   = false;
		s1_onoff_reset_armed = false;
		s1_onoff_acted      = false;
		s1EnterState(SM_S1_RESET, din);
		s1_prev_din = din;
		return s1_state;
	}

	/* ---- 2) 硬件复位态：reset 释放后 2s，市电正常回 T0，否则保持关机 ---- */
	if (s1_state == SM_S1_RESET) {
		if (!reset && (now - s1_entry_ms) >= S1_RESET_HOLD_MS) {
			s1EnterState(mains ? SM_S1_STANDBY : SM_S1_OFF_NO_MAINS, din);
		}
		s1_prev_din = din;
		return s1_state;
	}

	/* ---- 3) 软件复位：SYSTEM_ON_OFF 按满 >= 5s 后松手（任意状态）----
	 * 进入独立的 SW_RESET 子状态：先把 K3/K10 等全部输出关闭，稳定后再进 T0/T1。 */
	if (onoff_long) {
		s1EnterState(SM_S1_SW_RESET, din);
		s1_prev_din = din;
		return s1_state;
	}

	/* ---- 4) 软件复位态：输出已全断，等待 SW_RESET_MS 后按 Tx 判定进 T0/T1 ---- */
	if (s1_state == SM_S1_SW_RESET) {
		if ((now - s1_entry_ms) >= S1_SW_RESET_MS) {
			s1EnterState(mains ? SM_S1_STANDBY : SM_S1_OFF_NO_MAINS, din);
		}
		s1_prev_din = din;
		return s1_state;
	}


	/* ---- 状态转换 ---- */
	switch (s1_state) {

	case SM_S1_STANDBY:   /* T0 上电待机 */
		if (!mains) {
			s1EnterState(SM_S1_OFF_NO_MAINS, din);   /* 仅市电掉电 → T1 */
		} else if (onoff_2s && s1Check24V("开机")) {
			s1_onoff_acted = true;                   /* 本次按下已动作 → 不再关机 */
			s1EnterState(SM_S1_RUN, din);            /* 满 500ms 松手且 24V 正常 → 开机 */
		}
		break;

	case SM_S1_OFF_NO_MAINS:   /* T1 市电掉电 / OR 关机 */
		if (mains) {
			s1EnterState(SM_S1_STANDBY, din);        /* 市电恢复 → 回待机 */
		}
		break;

	case SM_S1_RUN:   /* T2 系统开机 */
		if (!mains) {
			s1EnterState(SM_S1_RUN_OR, din);         /* 开机后市电掉电 → OR(T3) */
		} else if (onoff_2s && !s1_onoff_acted && s1Check24V("关机")) {
			s1_onoff_acted = true;                   /* 本次按下已动作 */
			s1EnterState(SM_S1_SHUTDOWN, din);       /* 满 500ms 松手且 24V 正常 → 正常关机 */
		}
		break;

	case SM_S1_RUN_OR:   /* T3 开机后市电掉电（OR 模式） */
		if (mains) {
			s1EnterState(SM_S1_RUN, din);            /* 市电恢复回开机 */
		}
		else if (onoff_2s && !s1_onoff_acted && s1Check24V("关机")) {
			s1_onoff_acted = true;                   /* 本次按下已动作 */
			s1EnterState(SM_S1_SHUTDOWN, din);       /* OR 态下再按 → 正常关机(T4) */
		}
		break;

	case SM_S1_SHUTDOWN:   /* T4 正常关机（稳态：保持关机输出，等按键重新开机）*/
		if (!mains) {
			s1EnterState(SM_S1_OFF_NO_MAINS, din);   /* 市电掉电 → T1 */
		} else if (onoff_2s && !s1_onoff_acted && s1Check24V("开机")) {
			s1_onoff_acted = true;
			s1EnterState(SM_S1_RUN, din);            /* 再按（500ms~5s 松手）→ T2 */
		}
		break;

	default:
		break;
	}

	/* 每 1ms 重新应用当前状态的输出（幂等）：trolley 跟随 / K4,K5 推进 /
	 * T4 的 IS_PC、APP_HOST 位都随之刷新。 */
	s1Output(s1_state, din);

	/* 开关键按下期间：指示灯走双倍频率呼吸 */
	indicatorSetBreathFast(onoff);

	/* 全局指示灯：PD9 = S1 系统（S1 模式内常亮）；PD6 = 推车连接（与所在状态无关）*/
	bspDoutSetBitmap(BIT64(DOUT_LED_S1_SYS_ON), true);
	bspDoutSetBitmap(BIT64(DOUT_LED_TROLLEY_CONNECTED), isTrolleyConnectedDebounced());

	s1_prev_din = din;
	return s1_state;
}
