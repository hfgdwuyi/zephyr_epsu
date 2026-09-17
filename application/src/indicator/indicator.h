/*!
 * @file indicator.h
 * @brief 状态指示灯（DAC1_OUT2 / PA5）—— 应用层
 *
 * 由状态机在进入各状态时通过 indicatorSetMode() 设置：
 *   INDICATOR_BREATH : 待机 —— 0 → 1.5 V → 0 渐亮渐暗，周期 4000 ms（呼吸灯）
 *   INDICATOR_ON     : 开机运行 —— 恒定 1.5 V（常亮）
 *   INDICATOR_OFF    : 关机 / 复位 —— 0 V
 *   INDICATOR_MANUAL : 直接由 bspAoutWrite() 控制（调试用）
 *
 * 另外：开关键按下期间（indicatorSetBreathFast(true)）不论当前模式都改用同一呼吸
 * 波形、但周期减半（频率加倍），作为按键反馈；松开后恢复本来的模式。
 *
 * indicatorUpdate() 由 scheduler 的周期任务调用，按当前模式刷新 DAC 输出。
 * 底层 DAC 读写仍在 BSP（bsp_aout.c）。
 */
/*----------------------------------------------------------------------------*/
#ifndef INDICATOR_H
#define INDICATOR_H

#include <stdbool.h>
#include <stdint.h>

/* 状态指示灯工作模式 */
typedef enum {
	INDICATOR_OFF = 0,   /* 常灭 */
	INDICATOR_BREATH,    /* 呼吸灯：0→1.5V→0 渐亮渐暗（周期 4s） */
	INDICATOR_ON,        /* 常亮：恒定 1.5V */
	INDICATOR_MANUAL,    /* 由 bspAoutWrite 直接控制，indicatorUpdate 不干预 */
} indicatorMode_t;

/*! 设置指示灯模式（立即更新一次，避免下一次 update 前残留旧值） */
void indicatorSetMode(indicatorMode_t mode);

/*! 当前指示灯模式 */
indicatorMode_t indicatorGetMode(void);

/*! 呼吸频率加倍（true = 开关键按下期间）；只影响呼吸行为，不改变模式 */
void indicatorSetBreathFast(bool fast);

/*! 周期刷新（由 scheduler 调用），按模式驱动 DAC */
void indicatorUpdate(void);

#endif /* INDICATOR_H */
