/*
 * * stateMachine.h
 *
 * ePSU Power Supply State Machine — cios-zhong
 *
 * Manages system power states, relay sequencing, fault handling,
 * and mode transitions per the ePSU timing diagram.
 */

#ifndef STATEMACHINE_H
#define STATEMACHINE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- State definitions (matches state chart) ---------- */
typedef enum {
    STATEMACHINE_STATE_INIT = 0,          /* S1: ePSU boot-up / power-on init   */
    STATEMACHINE_STATE_SYS_ON,            /* S2: Sys_ON & Sys_startup check     */
    STATEMACHINE_STATE_PILOT_CONTACT,     /* S3: ePSU pilot contact (K3 close)  */
    STATEMACHINE_STATE_SWITCH_ON,         /* S4: switchOn after shutdown        */
    STATEMACHINE_STATE_NORMAL_OP,         /* S5: normal operation               */
    STATEMACHINE_STATE_S2_MODE,           /* S6: Power Saving mode for S2       */
    STATEMACHINE_STATE_CHARGING,          /* S7: charging power control         */
    STATEMACHINE_STATE_SHUTDOWN,          /* S8: normal shutdown sequence       */
    STATEMACHINE_STATE_FAULT,             /* S9: error/fault state              */
    STATEMACHINE_STATE_RESET,             /* S10: Reset process                 */
    STATEMACHINE_STATE_OFF,               /* S11: System powered off            */
} stateMachineState_t;

/* ---------- Error codes (from timing diagram) ---------- */
typedef enum {
    STATEMACHINE_ERR_NONE           = 0,
    STATEMACHINE_ERR_INIT_FAIL      = 0x01,   /* ESLR.35  — init error          */
    STATEMACHINE_ERR_K3_TIMEOUT     = 0x02,   /* ESTP3.36 — K3 close timeout    */
    STATEMACHINE_ERR_SWITCHON_FAIL  = 0x04,   /* ESIC1.37 — switchOn fail       */
    STATEMACHINE_ERR_MAINS_LOSS     = 0x08,   /* ESTP1.38 — mains PWR failure   */
    STATEMACHINE_ERR_RESET_RECOVERY = 0x10,   /* EAKO3.39 — reset after error   */
    STATEMACHINE_ERR_CHARGING_FAIL  = 0x20,   /* ESIC.40  — charging control err */
} stateMachineError_t;

/* ---------- System configuration modes ---------- */
typedef enum {
    STATEMACHINE_CFG_S1   = 0,   /* Single system mode    */
    STATEMACHINE_CFG_S2   = 1,   /* Dual system mode      */
    STATEMACHINE_CFG_SOLO = 2,   /* Solo operation        */
} stateMachineConfig_t;

/* ---------- Public API ---------- */

/* Init state machine (call once at boot) */
void stateMachineInit(void);

/* Main state machine tick — call periodically (e.g., every 1 ms) */
void stateMachineTick(void);

/* Get current state */
stateMachineState_t stateMachineGetState(void);

/* Get active fault bits */
uint32_t stateMachineGetFaults(void);

/* Get current error code */
stateMachineError_t stateMachineGetError(void);

/* Get latest error description */
const char *stateMachineGetErrorStr(stateMachineError_t err);

/* Request system shutdown */
void stateMachineRequestShutdown(void);

/* Request system reset */
void stateMachineRequestReset(void);

/* Request charging mode */
void stateMachineRequestCharging(void);

#ifdef __cplusplus
}
#endif

#endif /* STATEMACHINE_H */
