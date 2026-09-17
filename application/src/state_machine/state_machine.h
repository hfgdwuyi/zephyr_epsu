/*!
 * @file state_machine.h
 * @brief 顶层状态机 —— 两大主模式（S1 / S2）的判定与分派
 *
 * 两个主模式由配置拨码决定（依据 statemachine config.xlsx 的 mode config 表）：
 *
 *   pj2  pj3  pj4   mode
 *    0    0    x    invalid
 *    1    0    x    S1 system
 *    0    1    0    S2 class system
 *    0    1    1    S2 solo system
 *    1    1    x    invalid
 *
 * 注意：主模式只在上电后判定一次并锁定（见 state_machine.c::stateMachineTick）。
 * 运行中拨码变化不会重新判定、也不会切换系统；需重新选模式必须复位。
 *
 * S1 系统完整实现见 sm_s1.c/h（T0~T4 + RESET）。
 * S2 系统完整实现见 sm_s2.c/h（ON / OFF·待机 / OFF·非待机 / RESET，
 * 子模式 solo/classic 由 pj4 选择）。
 * 管脚定义（K*、两个管脚组、电平写入）也放在本文件里，S1/S2 共用。
 *
 * 对外接口只有下面两个：状态机不接受上位机指令，也没有查询 API ——
 * 状态信息在迁移时直接打印到串口（STATEMACHINE/S1/S2 前缀）。
 */
/*----------------------------------------------------------------------------*/
#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <zephyr/sys/util.h>   /* BIT64 */

#include "bsp_ain.h"           /* AIN_ADC_PDC0（24V 输出检测） */
#include "bsp_dio.h"           /* DOUT/DIN 索引（管脚定义、输入判定用） */
#include "sensor.h"            /* sensorGetPhys() */

/* ==================== K 继电器 / efuse ==================== */

/* 交流继电器 */
#define K2    BIT64(DOUT_K2_DRV)
#define K3    BIT64(DOUT_K3_DRV)
#define K4    BIT64(DOUT_K4_DRV)
#define K5    BIT64(DOUT_K5_DRV)
#define K6    BIT64(DOUT_K6_DRV)
#define K7    BIT64(DOUT_K7_DRV)

/* efuse */
#define K8_1  BIT64(DOUT_K8_1_EN)
#define K8_2  BIT64(DOUT_K8_2_EN)
#define K9    BIT64(DOUT_K9_EN)
#define K10   BIT64(DOUT_K10_EN)
#define K11   BIT64(DOUT_K11_EN)
#define K12   BIT64(DOUT_K12_EN)
#define K13   BIT64(DOUT_K13_EN)

/* ==================== 面板 LED ==================== */

#define LED_GRID_PWR_IN       BIT64(DOUT_LED_GRID_PWR_IN)          /* 市电输入指示     */
#define LED_UPS_IN            BIT64(DOUT_LED_UPS_IN)               /* UPS 输入         */
#define LED_SYS_ON            BIT64(DOUT_LED_SYSTEM_ON)            /* 系统开机指示     */
#define LED_S2_SOLO_SYS       BIT64(DOUT_LED_S2_SOLO_SYS)          /* S2 solo 指示     */
#define LED_TROLLEY_CONNECTED BIT64(DOUT_LED_TROLLEY_CONNECTED)    /* 推车连接         */
#define LED_IS_PC_ON          BIT64(DOUT_LED_IS_PC_ON)             /* IS_PC 指示       */
#define LED_APP_HOST_ON       BIT64(DOUT_LED_APP_HOST_ON)          /* APP_HOST 指示    */
#define LED_S1_SYS_ON         BIT64(DOUT_LED_S1_SYS_ON)            /* S1 系统指示      */
#define LED_S2_SYS_ON         BIT64(DOUT_LED_S2_SYS_ON)            /* S2 系统指示      */
#define LED_PWR24V_ON          BIT64(DOUT_LED_PWR_24V_ON)            /* 24V 输出         */
#define LED_CP24V_ON           BIT64(DOUT_LED_CP_24V_ON)            /* 24V 输出        */
#define LED_PAC230V_ON        BIT64(DOUT_LED_PAC230V_ON)           /* 230V 输出        */


/* ==================== 状态驱动 ==================== */

#define DRV_IS_PC_SITE    			BIT64(DOUT_DRV_IS_PC_SITE_ON)        /* IS_PC 侧驱动     */
#define DRV_APP_HOST      			BIT64(DOUT_DRV_APP_HOST_SITE_ON)     /* APP_HOST 侧驱动  */
#define DRV_MAINS_CONNECTED_MCU     BIT64(DOUT_MAINS_CONNECTED_MCU)      /* 市电检测->MCU    */
#define DRV_MAINS_CONNECTED_IS_PC   BIT64(DOUT_MAINS_CONNECTED_IS_PC)    /* 市电检测->IS_PC  */
#define DRV_TROLLEY_EN    			BIT64(DOUT_TROLLEY_ENABLE_DRV)       /* 推车使能         */
/* 推车连接状态输出（T2 时随 trolley 连接置位）*/
#define DRV_TRL_MU_MCU    			BIT64(DOUT_TRL_MU_CONNECTED_MCU)     /* 推车连接->MCU    */
#define DRV_TRL_MU_IS_PC  			BIT64(DOUT_TRL_MU_CONNECTED_IS_PC)   /* 推车连接->IS_PC  */

/* ==================== 管脚组 ==================== */

/* 全部继电器 / efuse */
#define SM_RELAY_ALL (K2 | K3 | K4 | K5 | K6 | K7 | K8_1 | K8_2 | \
		      K9 | K10 | K11 | K12 | K13)

/* 全部面板 LED */
#define SM_LED_ALL   (LED_GRID_PWR_IN | LED_PWR24V_ON | LED_CP24V_ON | LED_TROLLEY_CONNECTED | \
		      LED_UPS_IN | LED_SYS_ON | LED_S1_SYS_ON | LED_S2_SOLO_SYS | LED_S2_SYS_ON | \
		      LED_IS_PC_ON | LED_APP_HOST_ON)

/* 全部状态驱动输出（DRV_*，非 LED） */
#define SM_DRV_ALL   (DRV_IS_PC_SITE | DRV_APP_HOST | DRV_MAINS_CONNECTED_MCU | \
		      DRV_MAINS_CONNECTED_IS_PC | DRV_TROLLEY_EN | \
		      DRV_TRL_MU_MCU | DRV_TRL_MU_IS_PC)

/* ==================== 输入判定（DIN 位图谓词，S1/S2 共用） ==================== */
/* 都是"某个 DIN 位是否有效"的判定，布尔语义读成问句 → is 前缀 */

/* ME_BOX_ERROR 高 = 市电/整机正常；低 = 市电掉电或故障 */
static inline bool isMainsOk(uint32_t din)
{
	return (din & BIT(DIN_ME_BOX_ERROR)) != 0U;
}

static inline bool isTrolleyConnected(uint32_t din)
{
	return (din & BIT(DIN_TROLLEY_CONNECTED)) != 0U;
}

/* ==================== 上电判定 / 小车去抖参数 ==================== */
/* 主模式确认窗：出现确定拨码后必须连续保持该状态 >= 1s 才进入对应系统；
 * 期间若另一路配置也有效（冲突）或拨码变化 → 放弃，保持未进入。 */
#define SYSTEM_CONFIRM_MS    1000U
/* 小车连接去抖：有效电平需持续 >= 2s 才判为“已连接”；变为无效立即判为断开，
 * 需要再持续 2s 有效才重新判为连接。 */
#define TROLLEY_DEBOUNCE_MS  2000U

/*! 去抖后的小车连接状态（在 state_machine.c 每 tick 更新一次）。
 *  sm_s1/sm_s2 应使用本接口，而不是裸的 isTrolleyConnected(din)。 */
bool isTrolleyConnectedDebounced(void);

static inline bool isOnOffActive(uint32_t din)
{
	return (din & BIT(DIN_SYSTEM_ON_OFF)) != 0U;
}

static inline bool isResetActive(uint32_t din)
{
	return (din & BIT(DIN_SYSTEM_RESET)) != 0U;
}

static inline bool isPcOn(uint32_t din)
{
	return (din & BIT(DIN_IS_PC_ON)) != 0U;
}

static inline bool isAppHostOn(uint32_t din)
{
	return (din & BIT(DIN_APP_HOST_ON)) != 0U;
}

/* ==================== 24V 输出是否正常（AIN_ADC_PDC0 / PA6） ==================== */
/* 开机 / 正常关机的前置条件：24V 输出正常（sensor 换算后的 mV）。
 * 阈值按实际硬件标定调整。 */
#define SM_24V_AIN_CH   AIN_ADC_PDC0
#define SM_24V_MIN_MV   2200U

static inline bool is24VOk(void)
{
	return sensorGetPhys(SM_24V_AIN_CH) >= SM_24V_MIN_MV;
}

/* ==================== 写电平 ==================== */

/* 唯一的“组内全量写”：set 里置 1，grp 内其余置 0；grp 之外的位（其它组）不动。
 * 每个状态对三组各调一次 = 写出本状态的完整输出，且不会瞬断本状态要保留的路：
 *   doutWrite(SM_RELAY_ALL, ...) / doutWrite(SM_LED_ALL, ...) / doutWrite(SM_DRV_ALL, ...)
 * 注意：LED 硬件为低电平点亮（overlay 已标 GPIO_ACTIVE_LOW），本层一律用逻辑电平
 * （1 = 亮），物理极性由 gpio_pin_set_dt 按 DT 标志反转。 */
static inline void doutWrite(uint64_t grp, uint64_t set)
{
	bspDoutSetBitmap(set, true);
	bspDoutSetBitmap(grp & ~set, false);
}

/* 同上，但 keep 里的位保持不动（给 K4/K5 这类由别的逻辑单独控制的继电器用）*/
static inline void doutWriteKeep(uint64_t grp, uint64_t set, uint64_t keep)
{
	bspDoutSetBitmap(set, true);
	bspDoutSetBitmap(grp & ~set & ~keep, false);
}

/* 单独改某几位 → 直接用 BSP 位图接口，例：
 *   bspDoutSetBitmap(K4 | K5, false);                位清 0
 *   bspDoutSetBitmap(LED_TROLLEY_CONNECTED, true);   位置 1 */

/* ==================== 对外接口 ==================== */

/*! 初始化：不操作任何管脚，等第一次 tick 进入对应系统的初始子状态 */
void stateMachineInit(void);

/*! 每个 tick 推进一次（由 scheduler 的 1ms 线程调用） */
void stateMachineTick(void);

#endif /* STATE_MACHINE_H */
