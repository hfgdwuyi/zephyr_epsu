/*
 * psu_sm.h
 *
 * ePSU Power Supply State Machine — cios-zhong
 *
 * Manages system power states, relay sequencing, fault handling,
 * and mode transitions per the ePSU timing diagram.
 */

#ifndef PSU_SM_H
#define PSU_SM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- State definitions (matches state chart) ---------- */
typedef enum {
    PSU_STATE_INIT = 0,          /* S1: ePSU boot-up / power-on init   */
    PSU_STATE_SYS_ON,            /* S2: Sys_ON & Sys_startup check     */
    PSU_STATE_PILOT_CONTACT,     /* S3: ePSU pilot contact (K3 close)  */
    PSU_STATE_SWITCH_ON,         /* S4: switchOn after shutdown        */
    PSU_STATE_NORMAL_OP,         /* S5: normal operation               */
    PSU_STATE_S2_MODE,           /* S6: Power Saving mode for S2       */
    PSU_STATE_CHARGING,          /* S7: charging power control         */
    PSU_STATE_SHUTDOWN,          /* S8: normal shutdown sequence       */
    PSU_STATE_FAULT,             /* S9: error/fault state              */
    PSU_STATE_RESET,             /* S10: Reset process                 */
    PSU_STATE_OFF,               /* S11: System powered off            */
} psu_state_t;

/* ---------- Error codes (from timing diagram) ---------- */
typedef enum {
    PSU_ERR_NONE           = 0,
    PSU_ERR_INIT_FAIL      = 0x01,   /* ESLR.35  — init error          */
    PSU_ERR_K3_TIMEOUT     = 0x02,   /* ESTP3.36 — K3 close timeout    */
    PSU_ERR_SWITCHON_FAIL  = 0x04,   /* ESIC1.37 — switchOn fail       */
    PSU_ERR_MAINS_LOSS     = 0x08,   /* ESTP1.38 — mains PWR failure   */
    PSU_ERR_RESET_RECOVERY = 0x10,   /* EAKO3.39 — reset after error   */
    PSU_ERR_CHARGING_FAIL  = 0x20,   /* ESIC.40  — charging control err */
} psu_error_t;

/* ---------- System configuration modes ---------- */
typedef enum {
    PSU_CFG_S1   = 0,   /* Single system mode    */
    PSU_CFG_S2   = 1,   /* Dual system mode      */
    PSU_CFG_SOLO = 2,   /* Solo operation        */
} psu_config_t;

/* ---------- Public API ---------- */

/* Init state machine (call once at boot) */
void psu_sm_init(void);

/* Main state machine tick — call periodically (e.g., every 1 ms) */
void psu_sm_tick(void);

/* Get current state */
psu_state_t psu_sm_get_state(void);

/* Get active fault bits */
uint32_t psu_sm_get_faults(void);

/* Get current error code */
psu_error_t psu_sm_get_error(void);

/* Get latest error description */
const char *psu_sm_get_error_str(psu_error_t err);

/* Request system shutdown */
void psu_sm_request_shutdown(void);

/* Request system reset */
void psu_sm_request_reset(void);

/* Request charging mode */
void psu_sm_request_charging(void);

#ifdef __cplusplus
}
#endif

#endif /* PSU_SM_H */
