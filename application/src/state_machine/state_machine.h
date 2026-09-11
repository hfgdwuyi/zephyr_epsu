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

#include "bsp_dio.h"           /* DOUT 索引（管脚定义用） */

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

/* ==================== 面板 LED / 状态驱动 ==================== */

#define L_GRID_IN      BIT64(DOUT_LED_GRID_PWR_IN)         /* 市电输入指示     */
#define L_PWR24        BIT64(DOUT_LED_PWR_24_ON)           /* 24V 输出         */
#define L_CP224        BIT64(DOUT_LED_CP_224V_ON)          /* 224V 输出        */
#define L_TROLLEY      BIT64(DOUT_LED_TROLLEY_CONNECTED)   /* 推车连接         */
#define L_UPS_IN       BIT64(DOUT_LED_UPS_IN)              /* UPS 输入         */
#define L_SYS_ON       BIT64(DOUT_LED_SYSTEM_ON)           /* 系统开机指示     */
#define L_S2_SOLO_SYS  BIT64(DOUT_LED_S2_SOLO_SYS)         /* S2 solo 指示     */
#define L_S2_SYS_ON    BIT64(DOUT_LED_S2_SYS_ON)           /* S2 系统指示      */
#define L_IS_PC        BIT64(DOUT_LED_IS_PC_ON)            /* IS_PC 指示       */
#define L_APP_HOST     BIT64(DOUT_LED_APP_HOST_ON)         /* APP_HOST 指示    */
#define D_IS_PC_SITE   BIT64(DOUT_DRV_IS_PC_SITE_ON)
#define D_APP_HOST     BIT64(DOUT_DRV_APP_HOST_SITE_ON)
#define D_MAINS_MCU    BIT64(DOUT_MAINS_CONNECTED_MCU)
#define D_MAINS_IS_PC  BIT64(DOUT_MAINS_CONNECTED_IS_PC)
#define D_TROLLEY_EN   BIT64(DOUT_TROLLEY_ENABLE_DRV)

/* ==================== 管脚组 ==================== */

/* 全部继电器 / efuse */
#define SM_RELAY_ALL (K2 | K3 | K4 | K5 | K6 | K7 | K8_1 | K8_2 | \
		      K9 | K10 | K11 | K12 | K13)

/* 全部面板 LED / 状态驱动 */
#define SM_LED_ALL   (L_GRID_IN | L_PWR24 | L_CP224 | L_TROLLEY | L_UPS_IN | \
		      L_SYS_ON | L_S2_SOLO_SYS | L_S2_SYS_ON | L_IS_PC | \
		      L_APP_HOST | D_IS_PC_SITE | D_APP_HOST | D_MAINS_MCU | \
		      D_MAINS_IS_PC | D_TROLLEY_EN)

/* ==================== 写电平 ==================== */

static inline void relayWrite(uint64_t mask)
{
	bspDoutSetBitmap(mask, true);
	bspDoutSetBitmap(SM_RELAY_ALL & ~mask, false);
}

static inline void ledWrite(uint64_t mask)
{
	bspDoutSetBitmap(mask, true);
	bspDoutSetBitmap(SM_LED_ALL & ~mask, false);
}

/* ==================== 对外接口 ==================== */

/*! 初始化：不操作任何管脚，等第一次 tick 进入对应系统的初始子状态 */
void stateMachineInit(void);

/*! 每个 tick 推进一次（由 scheduler 的 1ms 线程调用） */
void stateMachineTick(void);

#endif /* STATE_MACHINE_H */
