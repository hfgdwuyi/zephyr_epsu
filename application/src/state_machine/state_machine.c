/*!
 * @file state_machine.c
 * @brief 顶层状态机 —— 主模式判定 + 系统分派 + 状态日志打印
 *
 * 设计要点：
 *   1. 每个 tick 读一次 DIN 快照，按 mode config 表判定当前主模式（S1 / S2 / 无效）
 *   2. S1 系统：全部交给 sm_s1.c 的 T0~T4/RESET 子状态（管脚由子状态设置）
 *   3. S2 系统：交给 sm_s2.c 的 ON/OFF(待机/非待机)/RESET 子状态（管脚由子状态设置）
 *   4. 拨码无效：不驱动任何输出（保持安全态）
 *   5. 系统切换 = 直接进入新系统的初始子状态；管脚只在子状态中控制，无退出清理
 *   6. 对外只有"跑"的接口：没有查询 API、也不接受上位机指令 ——
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
#include "bsp_dio.h"

/* 本模块 */
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

static const char *systemName(smSystem_t sys)
{
	switch (sys) {
	case SYSTEM_S1: return "S1";
	case SYSTEM_S2: return "S2";
	default:        return "none (invalid dip)";
	}
}

/* ==================== 主模式判定（mode config 表） ==================== */

/*   pj2  pj3  pj4   mode
 *    0    0    x    invalid
 *    1    0    x    S1 system
 *    0    1    0    S2 class system
 *    0    1    1    S2 solo system
 *    1    1    x    invalid
 */
static smSystem_t systemDetect(uint32_t din)
{
	const bool pj2 = (din & BIT(DIN_S1_SYSTEM_CONFIG)) != 0U;
	const bool pj3 = (din & BIT(DIN_S2_SYSTEM_CONFIG)) != 0U;

	if (pj2 && !pj3) {
		return SYSTEM_S1;
	}
	if (!pj2 && pj3) {
		
		return SYSTEM_S2;
	}
	return SYSTEM_NONE;
}

/* ==================== 生命周期 ==================== */

void stateMachineInit(void)
{
	g_system = SYSTEM_NONE;

	printk("STATEMACHINE: init (S1 + S2)\n");
}

void stateMachineTick(void)
{
	const uint32_t din = bspDinGetBitmap();   /* 一次 tick 共用一份输入快照 */

	const smSystem_t system = systemDetect(din);

	if (system != g_system) {
		g_system = system;

		switch (system) {
		case SYSTEM_S1:
			smS1Enter();
			break;
		case SYSTEM_S2:
			smS2Enter();
			break;
		default:
			break;      /* 拨码无效：不驱动任何输出 */
		}

		if (!terminalIsQuiet()) {
			printk("STATEMACHINE: system -> %s\n", systemName(system));
		}
	}

	switch (g_system) {
	case SYSTEM_S1:
		smS1Tick(din);
		break;

	case SYSTEM_S2:
		smS2Tick(din);
		break;

	default:
		break;
	}
}
