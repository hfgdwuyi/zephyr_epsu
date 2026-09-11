/*!
 * @file sm_s2.h
 * @brief S2 模式状态机 —— 依据 statemachine config.xlsx 的 s2 系列表
 *
 * S2 有 solo / classic 两种子模式（pj4 选择），输出表两者只在 PAC6 上不同，
 * 而 PAC6 在 pin_config.xlsx 中未填（待确认）→ 当前版本不控制 PAC6，
 * 因此两种子模式的管脚输出一致，仅模式指示灯不同。
 *
 * 状态（对应 Excel 的 s2 表）：
 *   ON            系统开机    ← "s2 <mode> mode on"
 *   OFF_STANDBY   关机·待机    ← "s2 <mode> mode off in standby"（市电在场）
 *   OFF_NO_STANDBY关机·非待机  ← "s2 <mode> mode off not standby"（市电掉电）
 *   RESET         硬件复位（与 S1 同规则：ME_BOX_ERROR && SYSTEM_RESET 边沿）
 *
 * 表中两列（连接推车 / 断开推车）作为状态内的输入维度处理，不单独成状态。
 * "switch off until X_Alive turn off" 的管脚 = 跟着对应输入保持合闸，
 * 直到 X 下电后才断开（K5←APP_HOST_ON，K9/K11←IS_PC_ON）。
 */
/*----------------------------------------------------------------------------*/
#ifndef SM_S2_H
#define SM_S2_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	SM_S2_RUN = 0,          /* 开机运行（Excel: s2 <mode> mode on）      */
	SM_S2_STANDBY,          /* 关机·待机：市电在场（Excel: off in standby）*/
	SM_S2_OFF_NO_MAINS,     /* 关机·非待机：市电掉电（off not standby）  */
	SM_S2_RESET,            /* 硬件复位                                  */
	SM_S2_STATE_COUNT
} smS2State_t;

/*! 进入 S2 系统：进入初始子状态（按市电在场与否进待机/非待机），由其设置管脚。 */
void smS2Enter(void);

/*!
 * @brief S2 系统每个 tick 的推进
 * @param din 当前 DIN 位图快照（bspDinGetBitmap()）
 * @return 当前状态
 */
smS2State_t smS2Tick(uint32_t din);

#endif /* SM_S2_H */
