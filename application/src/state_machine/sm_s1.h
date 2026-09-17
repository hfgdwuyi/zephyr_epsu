/*!
 * @file sm_s1.h
 * @brief S1 模式状态机（MU SYS 控制器）—— 依据 S1_MU_SYS_ctr_logic.md 的
 *        “S1 with Trolley” 页签（§4）
 *
 * S1 系统共有 5 个工作状态 + 硬件/软件复位，由 SYSTEM_ON_OFF1 按键、
 * 市电(ME_BOX)、外部复位共同驱动（当前版本暂不判断 trolley 连接）：
 *
 *   STANDBY      待机        ：市电正常、未开机（K3+K13 待机回路）
 *   OFF_NO_MAINS 关机·无市电  ：ME_BOX 异常 → 自动关机
 *   RUN          开机运行     ：按键开机后，AC/DC 各路依次使能（10ms 间隔）
 *   RUN_OR       开机运行·OR  ：开机过程中市电丢失 → 切 OR 模式供电（T3）
 *   SHUTDOWN     正常关机     ：按键关机；K9/K11 ← IS_PC_ON，K10 ← APP_HOST_ON
 *   RESET        硬件复位     ：SYSTEM_RESET 上升沿，全部重新初始化
 *   软件复位（非独立状态）     ：SYSTEM_ON_OFF 持续 ≥ 5s → 按 Tx 判定直接进 T0/T1
 *
 * 开关键时长（SYSTEM_ON_OFF1）：0.5s ≤ 按住 < 5s = 正常开机/关机；
 * ≥ 5s = 软件复位。
 */
/*----------------------------------------------------------------------------*/
#ifndef SM_S1_H
#define SM_S1_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	SM_S1_STANDBY = 0,      /* 待机：市电+推车就绪，未开机（md 的 T0）   */
	SM_S1_OFF_NO_MAINS,     /* 关机·市电掉电 / OR 关机（md 的 T1）      */
	SM_S1_RUN,              /* 开机运行：两轨 10ms 依次上电（md 的 T2） */
	SM_S1_RUN_OR,           /* 开机运行·市电掉电（OR 模式，md 的 T3）   */
	SM_S1_SHUTDOWN,         /* 正常关机（md 的 T4）                     */
	SM_S1_RESET,            /* 硬件复位                                 */
	SM_S1_SW_RESET,         /* 软件复位（先全断，再回 T0/T1）*/
	SM_S1_STATE_COUNT
} smS1State_t;

/*! 进入 S1 系统：直接进入初始子状态 STANDBY，由该子状态设置管脚。
 *  没有"退出"接口 —— 系统切换即进入新系统的子状态，管脚只在子状态中控制。 */
void smS1Enter(void);

/*!
 * @brief S1 系统每个 tick 的推进
 * @param din 当前 DIN 位图快照（bspDinGetBitmap()）
 * @return 当前状态
 */
smS1State_t smS1Tick(uint32_t din);

#endif /* SM_S1_H */
