/*!
 * @file sm_s2.h
 * @brief S2 state machine - per "S2 SOLO 逻辑功能测试.md" / "S2 Classis 逻辑功能测试.md"
 *
 * S2 has a solo / classic sub-mode (pj4) and the same state skeleton as S1:
 *   T0 standby / T1 off (mains lost) / T2 run / T3 UPS / T4 shutdown
 *   / hardware reset / software reset
 *
 * Only mains (ME_BOX_ERROR) drives transitions. Trolley is followed in T0/T2/T3/T4.
 *
 *   T0 STANDBY      : K3,K10 + MAINS_CONNECTED_MCU/IS_PC (+trolley LEDs/DRV)
 *   T1 OFF_NO_MAINS : all relays/LEDs/drivers off (wait for the supply to drop)
 *   T2 RUN          : solo    K3,K4,K6,K7,K8_1,K8_2,K9,K10,K11,K12,K13, K5 with trolley
 *                     classic K3,K4,K7,K8_1,K8_2,K9,K10,K11,K12,K13,    K5 with trolley
 *   T3 UPS          : K2,K8_1,K8_2,K9,K10,K11,K12,K13, K5 with trolley
 *                     (mains restored -> straight back to T2)
 *   T4 SHUTDOWN     : K3,K10 + IS_PC_ON -> K9,K11, APP_HOST_ON -> K5
 *   RESET           : all off
 *   SW_RESET        : key released after >= 5 s -> all off, then T0/T1
 *
 * On/off key (SYSTEM_ON_OFF): 0.5 s <= hold < 5 s = normal on/off (on release);
 * hold >= 5 s = software reset.
 */
/*----------------------------------------------------------------------------*/
#ifndef SM_S2_H
#define SM_S2_H

/* Standard library */
#include <stdbool.h>
#include <stdint.h>

typedef enum {
	SM_S2_STANDBY = 0,      /* standby (md T0)                      */
	SM_S2_OFF_NO_MAINS,     /* mains lost / UPS shutdown (md T1)    */
	SM_S2_RUN,              /* powered on (md T2)                   */
	SM_S2_UPS,           /* powered on, mains lost, UPS mode (md T3) */
	SM_S2_SHUTDOWN,         /* normal shutdown (md T4)              */
	SM_S2_RESET,            /* hardware reset                       */
	SM_S2_SW_RESET,         /* software reset (all off, then T0/T1) */
	SM_S2_STATE_COUNT
} smS2State_t;

/*! Enter the S2 system: goes straight to T0/T1 depending on mains presence. */
void smS2Enter(void);

/*!
 * @brief Advance the S2 system by one tick
 * @param din current DIN bitmap snapshot (bspDinGetBitmap())
 * @return current state
 */
smS2State_t smS2Tick(uint32_t din);

#endif /* SM_S2_H */
