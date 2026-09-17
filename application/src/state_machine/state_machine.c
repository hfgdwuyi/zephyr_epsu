/*!
 * @file state_machine.c
 * @brief 顶层状态机 —— 主模式判定 + 系统分派 + 状态日志打印
 *
 * 设计要点：
 *   1. 主模式只在上电后判定一次：确定拨码需连续保持 SYSTEM_CONFIRM_MS(1s) 才锁定；
 *      计时期间拨码变化、或两路同时有效（冲突）→ 放弃，保持未进入；
 *      锁定后拨码变化不再重新判定（运行中不切换系统）
 *   2. 拨码无效 / 冲突：不进入任何系统，也不驱动任何输出（保持安全态）
 *   2b. 小车连接信号去抖：有效需连续 >= TROLLEY_DEBOUNCE_MS(2s) 才判连接；
 *       变为无效立即判断开，之后需再持续 2s 有效才重新判连接
 *   3. S1 系统：全部交给 sm_s1.c 的 T0~T4/RESET 子状态（管脚由子状态设置）
 *   4. S2 系统：交给 sm_s2.c 的 ON/OFF(待机/非待机)/RESET 子状态（管脚由子状态设置）
 *   5. 对外只有"跑"的接口：没有查询 API、也不接受上位机指令 ——
 *      状态信息由本模块与各系统在状态迁移时直接打印到串口
 *      （STATEMACHINE: system -> … / S1: -> … / S2: -> …）
 *
 */
/*----------------------------------------------------------------------------*/
/* Zephyr */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

/* BSP */
#include "bsp_ain.h"    /* AIN_ADC_PDC0 */
#include "bsp_dio.h"    /* DOUT_LED_GRID_PWR_IN / DOUT_LED_UPS_IN */

/* Application */
#include "sensor.h"     /* sensorGetPhys() */
#include "state_machine.h"
#include "sm_s1.h"
#include "sm_s2.h"
#include "terminal.h"   /* terminalIsQuiet()：升级期间日志静默 */

/* ==================== 主模式（本模块内部） ==================== */

typedef enum {
	SYSTEM_NONE = 0,   /* 拨码无效：不进入任何模式 */
	SYSTEM_S1,         /* S1 单系统                  */
	SYSTEM_S2,         /* S2 双系统（solo / classic） */
} smSystem_t;

static smSystem_t g_system = SYSTEM_NONE;
static bool       g_system_locked;   /* true = 主模式已判定并锁定，不再重新判定 */

/* 主模式“待确认”状态：确定拨码出现后先计时，连续保持 SYSTEM_CONFIRM_MS 才生效 */
static smSystem_t g_pending = SYSTEM_NONE;
static bool       g_pending_timing;
static int64_t    g_pending_ms;

/* 小车连接去抖状态 */
static bool    g_trolley_conn;      /* 去抖后的连接状态 */
static bool    g_trolley_timing;
static int64_t g_trolley_since;

/* 每 tick 更新：有效电平持续 >= TROLLEY_DEBOUNCE_MS 才判连接；无效立即判断开 */
static void trolleyDebounce(uint32_t din)
{
	const bool raw = isTrolleyConnected(din);

	if (!raw) {
		g_trolley_conn   = false;
		g_trolley_timing = false;
		return;
	}
	if (g_trolley_conn) {
		return;   /* 已确认连接，保持 */
	}
	if (!g_trolley_timing) {
		g_trolley_timing = true;
		g_trolley_since  = k_uptime_get();
	} else if ((k_uptime_get() - g_trolley_since) >= TROLLEY_DEBOUNCE_MS) {
		g_trolley_conn = true;   /* 持续有效满 2s → 判连接 */
	}
}

bool isTrolleyConnectedDebounced(void)
{
	return g_trolley_conn;
}

static const char *systemName(smSystem_t sys)
{
	switch (sys) {
	case SYSTEM_S1: return "S1";
	case SYSTEM_S2: return "S2";
	default:        return "none (invalid dip)";
	}
}

/* ==================== 生命周期 ==================== */

void stateMachineInit(void)
{
	g_system = SYSTEM_NONE;
	g_system_locked = false;

	printk("STATEMACHINE: init\n");
}

void stateMachineTick(void)
{
	const uint32_t din = bspDinGetBitmap();   /* 一次 tick 共用一份输入快照 */

	/* 主模式判定：
	 *   - 未锁定时：确定拨码需连续保持 SYSTEM_CONFIRM_MS 才进入并锁定；
	 *   - 已锁定时：只有“两路配置都变低”才解除锁定（允许重新判定/切换模式）；
	 *     只要有一路为高，就保持当前模式，不做切换。 */
	const bool cfg_s1 = (din & BIT(DIN_S1_SYSTEM_CONFIG)) != 0U;
	const bool cfg_s2 = (din & BIT(DIN_S2_SYSTEM_CONFIG)) != 0U;

	if (g_system_locked) {
		if (!cfg_s1 && !cfg_s2) {
			g_system         = SYSTEM_NONE;   /* 拨码全释放 → 解锁，回到未判定 */
			g_system_locked  = false;
			g_pending        = SYSTEM_NONE;
			g_pending_timing = false;
			if (!terminalIsQuiet()) {
				printk("STATEMACHINE: dip released -> re-detect\n");
			}
		}
		/* 否则保持当前模式（不切换）*/
	} else {
		const bool s1 = cfg_s1;
		const bool s2 = cfg_s2;

		/* 判定（两路必须互斥，且各自需连续保持 SYSTEM_CONFIRM_MS 才生效）：
		 *   s1 有效 且 s2 无效 → S1
		 *   s1 无效 且 s2 有效 → S2
		 *   其它（同时有效 / 同时无效）→ 无效（不进入任何系统）*/
		smSystem_t want;

		if (s1 && !s2) {
			want = SYSTEM_S1;
		} else if (!s1 && s2) {
			want = SYSTEM_S2;
		} else {
			want = SYSTEM_NONE;
		}

		if (want == SYSTEM_NONE) {
			g_pending        = SYSTEM_NONE;
			g_pending_timing = false;    /* 无效/冲突 → 取消，保持未进入 */
		} else if (want != g_pending || !g_pending_timing) {
			g_pending        = want;     /* 新的确定模式：重新开始 1s 计时 */
			g_pending_timing = true;
			g_pending_ms     = k_uptime_get();
		} else if ((k_uptime_get() - g_pending_ms) >= SYSTEM_CONFIRM_MS) {
			g_system        = want;      /* 连续保持满 1s → 锁定并进入 */
			g_system_locked = true;

			/* 先报主模式，再进子状态 —— 日志顺序为
			 * "STATEMACHINE: system -> S1" 之后才是 "S1: -> STANDBY ..." */
			if (!terminalIsQuiet()) {
				printk("STATEMACHINE: system -> %s\n", systemName(want));
			}

			switch (want) {
			case SYSTEM_S1: smS1Enter(); break;
			case SYSTEM_S2: smS2Enter(); break;
			default:        break;
			}
		}
	}

	/* 小车连接信号去抖（每 tick 一次，sm_s1/sm_s2 通过接口读结果）*/
	trolleyDebounce(din);

	switch (g_system) {
	case SYSTEM_S1:
		smS1Tick(din);
		break;

	case SYSTEM_S2:
		smS2Tick(din);
		break;

	case SYSTEM_NONE:
		/* 未判定（拨码无效/冲突）或已解锁：不驱动任何输出（安全态）*/
		doutWrite(SM_RELAY_ALL, 0);
		doutWrite(SM_LED_ALL, 0);
		doutWrite(SM_DRV_ALL, 0);
		break;
	}

	/* ---- 市电/UPS 指示（PD2/PD3，全局）----
	 * 判据：ME_BOX_ERROR 高电平 且 PA6(AIN_ADC_PDC0) 转换电压 > 2.2 V。
	 * 成立 → 亮 mains(PD2)；不成立 → 亮 UPS(PD3)。 */
	const bool mains_ok = isMainsOk(din) &&
			      (sensorGetPhys(AIN_ADC_PDC0) > 2200U);

	bspDoutSetBitmap(BIT64(DOUT_LED_GRID_PWR_IN), mains_ok);   /* PD2 mains */
	bspDoutSetBitmap(BIT64(DOUT_LED_UPS_IN), !mains_ok);       /* PD3 UPS  */

	/* ---- IS_PC / APP_HOST 指示（PD7/PD12，全局）----
	 * 跟随输入信号：IS_PC_ON 有效 → 亮 PD7；APP_HOST_ON 有效 → 亮 PD12。 */
	bspDoutSetBitmap(LED_IS_PC_ON, isPcOn(din));               /* PD7  */
	bspDoutSetBitmap(LED_APP_HOST_ON, isAppHostOn(din));       /* PD12 */
}
