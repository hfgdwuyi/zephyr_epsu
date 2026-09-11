/*!
 * @file sm_s1.h
 * @brief S1 模式状态机（MU SYS 控制器）—— 依据 S1_MU_SYS_ctr_logic_state_machine.md
 *
 * S1 系统共有 5 个工作状态 + 复位，由 SYSTEM_ON_OFF1 按键、市电(ME_BOX)、
 * 推车连接、外部复位共同驱动：
 *
 *   T0 上电待机          ：市电正常、推车连接、未开机（K3+K13 待机回路）
 *   T1 市电掉电/OR关机    ：ME_BOX 异常 → 自动关机
 *   T2 系统开机          ：按键开机后，AC/DC 各路依次使能（10ms 间隔）
 *   T3 开机后市电掉电     ：开机过程中市电丢失 → 切 OR 模式供电
 *   T4 正常关机          ：按键关机，回到待机回路；K9/K10 由 IS_PC / APP_HOST 决定
 *   RESET 硬件复位        ：ME_BOX_ERROR && SYSTEM_RESET 边沿，全部重新初始化
 */
/*----------------------------------------------------------------------------*/
#ifndef SM_S1_H
#define SM_S1_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	SM_S1_T0 = 0,   /* 上电待机                                 */
	SM_S1_T1,       /* 市电掉电或 OR 模式下关机                  */
	SM_S1_T2,       /* 系统开机                                 */
	SM_S1_T3,       /* 开机后市电掉电（OR 模式）                 */
	SM_S1_T4,       /* 正常关机                                 */
	SM_S1_RESET,    /* 硬件复位                                 */
	SM_S1_STATE_COUNT
} smS1State_t;

/*! 进入 S1 系统：直接进入初始子状态 T0（上电待机），由该子状态设置管脚。
 *  没有"退出"接口 —— 系统切换即进入新系统的子状态，管脚只在子状态中控制。 */
void smS1Enter(void);

/*!
 * @brief S1 系统每个 tick 的推进
 * @param din 当前 DIN 位图快照（bspDinGetBitmap()）
 * @return 当前状态
 */
smS1State_t smS1Tick(uint32_t din);

#endif /* SM_S1_H */
