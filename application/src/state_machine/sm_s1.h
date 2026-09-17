/*!
 * @file sm_s1.h
 * @brief S1 state machine (MU SYS controller) - per "S1逻辑功能测试.md"
 *
 * S1 has 5 working states plus hardware/software reset, driven by the
 * SYSTEM_ON_OFF1 key, mains (ME_BOX_ERROR) and SYSTEM_RESET:
 *
 *   STANDBY      T0 standby        : mains OK, not powered on; K3,K10 +
 *                                    K9/K11 <- IS_PC_ON, K13 <- APP_HOST_ON
 *   OFF_NO_MAINS T1 off / no mains : everything off, wait for the supply to drop
 *   RUN          T2 powered on     : K3,K6,K7,K8_1,K9,K10,K11,K12,K13 (+K4,K5 trolley)
 *   UPS          T3 UPS mode       : mains lost while running; K2,K10 (+K9/K11 ; K13)
 *                                    mains restored -> back to T0
 *   SHUTDOWN     T4 normal shutdown: K3,K10; K9/K11 <- IS_PC_ON, K13 <- APP_HOST_ON
 *   RESET        hardware reset    : SYSTEM_RESET rising edge, full re-init
 *   SW_RESET     software reset    : SYSTEM_ON_OFF released after >= 5s -> T0/T1
 *
 * On/off key (SYSTEM_ON_OFF1): 0.5 s <= hold < 5 s = normal on/off (on release);
 * hold >= 5 s = software reset.
 */
/*----------------------------------------------------------------------------*/
#ifndef SM_S1_H
#define SM_S1_H

/* Standard library */
#include <stdbool.h>
#include <stdint.h>

typedef enum {
	SM_S1_STANDBY = 0,      /* standby, not powered on (md T0)     */
	SM_S1_OFF_NO_MAINS,     /* off: mains lost / UPS shutdown (md T1) */
	SM_S1_RUN,              /* powered on (md T2)                   */
	SM_S1_UPS,           /* powered on, mains lost, UPS mode (md T3) */
	SM_S1_SHUTDOWN,         /* normal shutdown (md T4)              */
	SM_S1_RESET,            /* hardware reset                       */
	SM_S1_SW_RESET,         /* software reset (all off, then T0/T1) */
	SM_S1_STATE_COUNT
} smS1State_t;

/*! Enter the S1 system: goes straight to STANDBY/OFF_NO_MAINS, which drives
 *  the pins. There is no "exit" call - switching systems just enters the other
 *  system's sub-state, and pins are only driven from sub-states. */
void smS1Enter(void);

/*!
 * @brief Advance the S1 system by one tick
 * @param din current DIN bitmap snapshot (bspDinGetBitmap())
 * @return current state
 */
smS1State_t smS1Tick(uint32_t din);

#endif /* SM_S1_H */
