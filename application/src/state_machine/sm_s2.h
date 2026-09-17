/*!
 * @file sm_s2.h
 * @brief S2 模式状态机 —— 依据 S1_MU_SYS_ctr_logic.md §5/§6
 *
 * S2 有 solo / classic 两种子模式（pj4 选择），状态骨架与 S1 完全同构：
 *   T0 上电待机 / T1 市电掉电(OR)关机 / T2 系统开机 / T3 开机后市电掉电(OR)
 *   / T4 正常关机 / 硬件复位 / 软件复位
 *
 * 判定只看市电 ME_BOX_ERROR；**只有 T2 判断 trolley 连接**（动态开关
 * LED_TROLLEY_CONNECTED / TROLLEY_ENABLE_DRV），其它状态都不考虑 trolley。
 *
 * Solo / classic 的差异：模式指示灯（S2_SYS ± S2_SOLO_SYS）与 T3(OR) 的
 * 继电器集合（Solo 多 K5/K8_2）。
 *
 *   T0 STANDBY      上电待机        ：K3,K13
 *   T1 OFF_NO_MAINS 市电掉电/OR关机 ：K3,K13
 *   T2 RUN          系统开机        ：K3,K4,K5,K6,K7,K8_1,K9,K11,K12,K13
 *   T3 RUN_OR       开机后市电掉电  ：Solo K2,K5,K8_1,K8_2,K9,K10,K11,K12,K13
 *                                    Chassis K2,K8_1,K9,K10,K11,K12,K13
 *   T4 SHUTDOWN     正常关机        ：K3,K13 + IS_PC→K9, APP_HOST→K5,K11
 *   RESET                           ：全断（硬件复位）
 *   SW_RESET 软件复位               ：长按 ≥ 5s 松手 → 先全断（含 K3/K10）→ 再进 T0/T1
 *
 * 开关键（SYSTEM_ON_OFF）：0.5s ≤ 按住 < 5s = 正常开机/关机（释放时生效）；
 * ≥ 5s 且本次按下未触发过 short = 软件复位。
 */
/*----------------------------------------------------------------------------*/
#ifndef SM_S2_H
#define SM_S2_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	SM_S2_STANDBY = 0,      /* 待机（md 的 T0）                          */
	SM_S2_OFF_NO_MAINS,     /* 市电掉电 / OR 关机（md 的 T1）            */
	SM_S2_RUN,              /* 系统开机（md 的 T2，唯一判断 trolley）    */
	SM_S2_RUN_OR,           /* 开机后市电掉电 OR（md 的 T3）             */
	SM_S2_SHUTDOWN,         /* 正常关机（md 的 T4）                      */
	SM_S2_RESET,            /* 硬件复位                                  */
	SM_S2_SW_RESET,         /* 软件复位（先全断，再回 T0/T1）*/
	SM_S2_STATE_COUNT
} smS2State_t;

/*! 进入 S2 系统：按市电在场与否直接进 T0/T1，由该子状态设置管脚。 */
void smS2Enter(void);

/*!
 * @brief S2 系统每个 tick 的推进
 * @param din 当前 DIN 位图快照（bspDinGetBitmap()）
 * @return 当前状态
 */
smS2State_t smS2Tick(uint32_t din);

#endif /* SM_S2_H */
