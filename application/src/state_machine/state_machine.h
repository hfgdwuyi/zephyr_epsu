/*!
 * @file state_machine.h
 * @brief Top-level state machine - main mode (S1 / S2) detection and dispatch
 *
 * Main mode is selected by DIP switches (mode config table):
 *
 *   pj2  pj3  pj4   mode
 *    0    0    x    invalid
 *    1    0    0    S1 system
 *    1    0    1    invalid (S1 and solo both active -> do not enter)
 *    0    1    0    S2 class system
 *    0    1    1    S2 solo system
 *    1    1    x    invalid
 *
 * NOTE: solo (pj4) is only meaningful under S2; an active pj4 vetoes S1 (avoids S1/solo coupling).
 *
 * Main mode is latched once after power-on (see state_machine.c::stateMachineTick).
 * Later DIP changes do not re-evaluate or switch the system; to re-select, reset.
 *
 * Full S1 implementation: sm_s1.c/h (T0~T4 + RESET).
 * Full S2 implementation: sm_s2.c/h (T0~T4 + RESET; sub-mode solo/classic via pj4).
 * Pin definitions (K*, pin groups, level writers) also live here, shared by S1/S2.
 *
 * Only the two API calls below: no host commands and no query API -
 * state info is printed to the console on transitions (STATEMACHINE/S1/S2 prefix).
 */
/*----------------------------------------------------------------------------*/
#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

/* Zephyr */
#include <zephyr/sys/util.h>

/* BSP */
#include "bsp_ain.h"
#include "bsp_dio.h"

/* Application */
#include "sensor.h"

/* ==================== K relays / efuse ==================== */

/* AC relays */
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

/* ==================== Panel LEDs ==================== */

#define LED_GRID_PWR_IN       BIT64(DOUT_LED_GRID_PWR_IN)          /* Grid/mains input    */
#define LED_UPS_IN            BIT64(DOUT_LED_UPS_IN)               /* UPS input           */
#define LED_SYS_ON            BIT64(DOUT_LED_SYSTEM_ON)            /* System on           */
#define LED_S2_SOLO_SYS       BIT64(DOUT_LED_S2_SOLO_SYS)          /* S2 solo             */
#define LED_TROLLEY_CONNECTED BIT64(DOUT_LED_TROLLEY_CONNECTED)    /* Trolley connected   */
#define LED_IS_PC_ON          BIT64(DOUT_LED_IS_PC_ON)             /* IS_PC               */
#define LED_APP_HOST_ON       BIT64(DOUT_LED_APP_HOST_ON)          /* APP_HOST            */
#define LED_S1_SYS_ON         BIT64(DOUT_LED_S1_SYS_ON)            /* S1 system           */
#define LED_S2_SYS_ON         BIT64(DOUT_LED_S2_SYS_ON)            /* S2 system           */
#define LED_PWR24V_ON          BIT64(DOUT_LED_PWR_24V_ON)            /* 24V output          */
#define LED_CP24V_ON           BIT64(DOUT_LED_CP_24V_ON)            /* CP 24V output       */
#define LED_PAC230V_ON        BIT64(DOUT_LED_PAC230V_ON)           /* PAC 230V output     */


/* ==================== Status output drivers ==================== */

#define DRV_IS_PC_SITE    			BIT64(DOUT_DRV_IS_PC_SITE_ON)        /* IS_PC site driver   */
#define DRV_APP_HOST      			BIT64(DOUT_DRV_APP_HOST_SITE_ON)     /* APP_HOST site driver*/
#define DRV_MAINS_CONNECTED_MCU     BIT64(DOUT_MAINS_CONNECTED_MCU)      /* Mains detect -> MCU */
#define DRV_MAINS_CONNECTED_IS_PC   BIT64(DOUT_MAINS_CONNECTED_IS_PC)    /* Mains detect -> IS_PC*/
#define DRV_TROLLEY_EN    			BIT64(DOUT_TROLLEY_ENABLE_DRV)       /* Trolley enable      */
/* Trolley-connected status outputs (set with trolley in T2) */
#define DRV_TRL_MU_MCU    			BIT64(DOUT_TRL_MU_CONNECTED_MCU)     /* Trolley -> MCU      */
#define DRV_TRL_MU_IS_PC  			BIT64(DOUT_TRL_MU_CONNECTED_IS_PC)   /* Trolley -> IS_PC    */

/* ==================== Pin groups ==================== */

/* All relays / efuse */
#define SM_RELAY_ALL (K2 | K3 | K4 | K5 | K6 | K7 | K8_1 | K8_2 | \
		      K9 | K10 | K11 | K12 | K13)

/* All panel LEDs */
#define SM_LED_ALL   (LED_GRID_PWR_IN | LED_PWR24V_ON | LED_CP24V_ON | LED_TROLLEY_CONNECTED | \
		      LED_UPS_IN | LED_SYS_ON | LED_S1_SYS_ON | LED_S2_SOLO_SYS | LED_S2_SYS_ON | \
		      LED_IS_PC_ON | LED_APP_HOST_ON)

/* All status drivers (DRV_*, not LEDs) */
#define SM_DRV_ALL   (DRV_IS_PC_SITE | DRV_APP_HOST | DRV_MAINS_CONNECTED_MCU | \
		      DRV_MAINS_CONNECTED_IS_PC | DRV_TROLLEY_EN | \
		      DRV_TRL_MU_MCU | DRV_TRL_MU_IS_PC)

/* ==================== Input predicates (DIN bitmap, shared by S1/S2) ==================== */
/* All test whether a DIN bit is active; boolean semantics -> is-prefix */

/* ME_BOX_ERROR high = mains/unit OK; low = mains lost or fault */
static inline bool isMainsOk(uint32_t din)
{
	return (din & BIT(DIN_ME_BOX_ERROR)) != 0U;
}

static inline bool isTrolleyConnected(uint32_t din)
{
	return (din & BIT(DIN_TROLLEY_CONNECTED)) != 0U;
}

/* ==================== Power-on detection / trolley debounce ==================== */
/* Main-mode confirmation window: a valid DIP pattern must hold >= 1s before
 * entering its system; a conflicting/other DIP change aborts it. */
#define SYSTEM_CONFIRM_MS    1000U
/* Trolley debounce: a valid level must hold >= 2s to be treated as connected;
 * an invalid level disconnects immediately and needs another 2s to reconnect. */
#define TROLLEY_DEBOUNCE_MS  2000U

/*! Debounced trolley-connected state (updated every tick in state_machine.c).
 *  sm_s1/sm_s2 must use this instead of the raw isTrolleyConnected(din). */
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

/* ==================== 24V output OK (AIN_ADC_PDC0 / PA6) ==================== */
/* Precondition for power-on / normal shutdown: 24V output OK (scaled mV).
 * Threshold must be calibrated for the actual hardware. */
#define SM_24V_AIN_CH   AIN_ADC_PDC0
#define SM_24V_MIN_MV   2200U

static inline bool is24VOk(void)
{
	return sensorGetPhys(SM_24V_AIN_CH) >= SM_24V_MIN_MV;
}

/* ==================== Level writers ==================== */

/* The single full-group writer: bits in 'set' go 1, the rest of 'grp' go 0;
 * bits outside 'grp' (other groups) are untouched. Each state calls it once per
 * group = the state's complete output, without glitching relays it keeps on:
 *   doutWrite(SM_RELAY_ALL, ...) / doutWrite(SM_LED_ALL, ...) / doutWrite(SM_DRV_ALL, ...)
 * NOTE: panel LEDs are active-low in hardware (GPIO_ACTIVE_LOW in the overlay);
 * this layer uses logical levels (1 = lit), the polarity is applied by gpio_pin_set_dt. */
static inline void doutWrite(uint64_t grp, uint64_t set)
{
	bspDoutSetBitmap(set, true);
	bspDoutSetBitmap(grp & ~set, false);
}

/* Same as above but bits in 'keep' stay untouched (for relays such as K4/K5
 * that are driven separately). */
static inline void doutWriteKeep(uint64_t grp, uint64_t set, uint64_t keep)
{
	bspDoutSetBitmap(set, true);
	bspDoutSetBitmap(grp & ~set & ~keep, false);
}

/* To change individual bits, use the BSP bitmap API directly, e.g.:
 *   bspDoutSetBitmap(K4 | K5, false);                clear bits
 *   bspDoutSetBitmap(LED_TROLLEY_CONNECTED, true);   set bits */

/* ==================== Public API ==================== */

/*! Init: does not drive any pin; the first tick enters the initial sub-state */
void stateMachineInit(void);

/*! Advance one tick (called by the scheduler's 1 ms thread) */
void stateMachineTick(void);

#endif /* STATE_MACHINE_H */
