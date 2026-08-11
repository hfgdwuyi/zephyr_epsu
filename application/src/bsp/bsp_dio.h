/*
 * Copyright © Siemens Healthcare GmbH 2022, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 * @file
 * @brief   Header file for bsp_dio.c (Zephyr port) — cios-zhong
 */
/*----------------------------------------------------------------------------*/
#ifndef BSP_DIO_H
#define BSP_DIO_H

#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== DOUT index — pin_config.xlsx ====================
 * The DOUT index of a pin IS the `reg` of its child of dout_config in
 * application/app.overlay — that file is the single source of truth.
 * Inserting a pin means editing app.overlay only; these macros (and the
 * bsp_dio.c spec array, keyed by reg) follow automatically.
 *
 * Contract: reg values must stay dense 0..N-1 (DOUT_MAX = N). The 64-bit
 * DOUT bitmap splits at bit 32 (bsp_dio.c word = i/32) and dout_specs[i]
 * == reg i, so a gap or reordering would silently shift pins. */

#define DOUT_IDX(label) DT_REG_ADDR(DT_NODELABEL(label))

/* Trolley enable */
#define DOUT_TROLLEY_ENABLE_DRV    DOUT_IDX(trolley_enable_drv)   /* PH1  */

/* External watchdog (MAX6703A WDI) */
#define DOUT_WDI                   DOUT_IDX(wdi)                  /* PH9  */

/* Power good / K13 enable */
#define DOUT_PG_13V5               DOUT_IDX(pg_13v5)              /* PA12 */
#define DOUT_K13_EN                DOUT_IDX(k13_en)               /* PB6  */

/* Debug LEDs */
#define DOUT_DBG_LED0              DOUT_IDX(dbg_led0)             /* PC8  */
#define DOUT_DBG_LED1              DOUT_IDX(dbg_led1)             /* PC9  */
#define DOUT_DBG_LED2              DOUT_IDX(dbg_led2)             /* PC10 */

/* Panel LED indicators
 * Note: PD8 (led_pac230v_on) / PD9 (led_s1_sys_on) removed — USART3 */
#define DOUT_LED_PWR_24_ON         DOUT_IDX(led_pwr_24_on)        /* PD0  */
#define DOUT_LED_CP_224V_ON        DOUT_IDX(led_cp_224v_on)       /* PD1  */
#define DOUT_LED_GRID_PWR_IN       DOUT_IDX(led_grid_pwr_in)      /* PD2  */
#define DOUT_LED_UPS_IN            DOUT_IDX(led_ups_in)           /* PD3  */
#define DOUT_LED_SYSTEM_ON         DOUT_IDX(led_system_on)        /* PD4  */
#define DOUT_LED_S2_SOLO_SYS       DOUT_IDX(led_s2_solo_sys)      /* PD5  */
#define DOUT_LED_TROLLEY_CONNECTED DOUT_IDX(led_trolley_connected) /* PD6  */
#define DOUT_LED_IS_PC_ON          DOUT_IDX(led_is_pc_on)         /* PD7  */
#define DOUT_LED_S2_SYS_ON         DOUT_IDX(led_s2_sys_on)        /* PD10 */
#define DOUT_LED_APP_HOST_ON       DOUT_IDX(led_app_host_on)      /* PD12 */

/* Relay / power drivers (K2..K13) */
#define DOUT_K5_DRV                DOUT_IDX(k5_drv)               /* PI4  */
#define DOUT_K6_DRV                DOUT_IDX(k6_drv)               /* PI5  */
#define DOUT_K3_DRV                DOUT_IDX(k3_drv)               /* PI6  */
#define DOUT_K2_DRV                DOUT_IDX(k2_drv)               /* PI7  */
#define DOUT_K4_DRV                DOUT_IDX(k4_drv)               /* PI8  */
#define DOUT_K7_DRV                DOUT_IDX(k7_drv)               /* PI9  */
#define DOUT_K10_EN                DOUT_IDX(k10_en)               /* PI10 */
#define DOUT_K11_EN                DOUT_IDX(k11_en)               /* PI11 */
#define DOUT_K12_EN                DOUT_IDX(k12_en)               /* PI12 */
#define DOUT_K8_1_EN               DOUT_IDX(k8_1_en)              /* PI13 */
#define DOUT_K8_2_EN               DOUT_IDX(k8_2_en)              /* PI14 */
#define DOUT_K9_EN                 DOUT_IDX(k9_en)                /* PI15 */

/* Status output drivers */
#define DOUT_DRV_IS_PC_SITE_ON     DOUT_IDX(drv_is_pc_site_on)    /* PJ11 */
#define DOUT_DRV_APP_HOST_SITE_ON  DOUT_IDX(drv_app_host_site_on) /* PJ12 */
#define DOUT_MAINS_CONNECTED_IS_PC DOUT_IDX(mains_connected_is_pc) /* PJ13 */
#define DOUT_MAINS_CONNECTED_MCU   DOUT_IDX(mains_connected_mcu)  /* PJ14 */

/* ==================== DIN index — pin_config.xlsx ====================
 * Same scheme as DOUT: each DIN_xxx derives from the `reg` of its child of
 * din_config in application/app.overlay. Note some names differ from the
 * node label (e.g. DIN_LED_PWR_24_ON ↔ led_pwr_24_fb). */

#define DIN_IDX(label) DT_REG_ADDR(DT_NODELABEL(label))

#define DIN_GRID_MAIN_RELAY_STATUS  DIN_IDX(grid_main_relay_status) /* PH5  — high=valid, low=invalid */
#define DIN_ME_BOX_ERROR            DIN_IDX(me_box_error)           /* PH6  — high=normal, low=fault */
#define DIN_FAULT0                  DIN_IDX(fault0)                 /* PA8  */
#define DIN_FAULT1                  DIN_IDX(fault1)                 /* PA9  */
#define DIN_FAULT2                  DIN_IDX(fault2)                 /* PA10 */
#define DIN_FAULT3                  DIN_IDX(fault3)                 /* PA11 */
#define DIN_TEMP_ALERT              DIN_IDX(temp_alert)             /* PB12 */
#define DIN_LED_PWR_24_ON           DIN_IDX(led_pwr_24_fb)          /* PC6  */
#define DIN_FAULT4                  DIN_IDX(fault4)                 /* PC11 */
#define DIN_FAULT5                  DIN_IDX(fault5)                 /* PC12 */
#define DIN_FAULT6                  DIN_IDX(fault6)                 /* PC13 */
#define DIN_TRL_MU_CONNECTED_MCU    DIN_IDX(trl_mu_connected_mcu)   /* PD13 */
#define DIN_TRL_MU_CONNECTED_IS_PC  DIN_IDX(trl_mu_connected_is_pc) /* PD14 */
#define DIN_SYSTEM_ON_OFF           DIN_IDX(system_on_off)          /* PJ0  */
#define DIN_SYSTEM_RESET            DIN_IDX(system_reset)           /* PJ1  */
#define DIN_S1_SYSTEM_CONFIG        DIN_IDX(s1_system_config)       /* PJ2  */
#define DIN_S2_SYSTEM_CONFIG        DIN_IDX(s2_system_config)       /* PJ3  */
#define DIN_SOLO_SYSTEM_CONFIG      DIN_IDX(solo_system_config)     /* PJ4  */
#define DIN_TROLLEY_CONNECTED       DIN_IDX(trolley_connected)      /* PJ5  — high=connected */
#define DIN_APP_HOST_ON             DIN_IDX(app_host_on)            /* PJ7  */
#define DIN_SMART_WHS_INDICATE      DIN_IDX(smart_whs_indicate)     /* PJ8  */
#define DIN_DRAWER_INDICATE         DIN_IDX(drawer_indicate)        /* PJ9  */
#define DIN_SMART_CTRL_WHS_SEARCH   DIN_IDX(smart_ctrl_whs_search)  /* PJ10 */
#define DIN_IS_PC_ON                DIN_IDX(is_pc_on)               /* PJ15 */

/* ==================== Fan PWM index (via bsp_pwm) ==================== */

enum {
	FAN_PWM1 = 0,  /* PJ15 — fan1_pwm */
	FAN_PWM2 = 1,  /* PI15 — fan2_pwm */
};

/* ==================== Public types ==================== */

typedef struct {
	bool    deb_en;    /* Debouncing enabled */
	uint8_t deb_time;  /* Debouncing time (in update ticks; 1ms per tick) */
} bspDinSettings_t;

extern const uint8_t dinMax;
extern const uint8_t doutMax;

/* ==================== Public API ==================== */

void bspDioInit(void);

/* Configure debouncing for a DIN pin index */
void bspDinSetDebouncing(uint8_t pin, bspDinSettings_t settings);

/* ---- DIN: bitmap-based state (updated by bspDinUpdate, called from scheduler) ----
 * bspDinUpdate() samples all DIN pins into the internal bitmap.
 * bspDinGet()/bspDinGetBitmap() read the sampled state. */

void bspDinUpdate(void);
uint32_t bspDinGetBitmap(void);

static inline bool bspDinGet(uint8_t pin)
{
	return (bspDinGetBitmap() & BIT(pin)) != 0U;
}

/* ---- DOUT: bitmap-based control ----
 * The public interface is the DOUT bitmap only: bspDoutSetBitmap() is the
 * sole write entry (single pins via a BIT64(pin) mask) and bspDoutGetBitmap()
 * the read entry. The bitmap is 64-bit because DOUT_MAX = 33 pins (indices
 * 0..32) exceeds one 32-bit word. bspDoutUpdate() (called from the scheduler
 * every 1 ms) applies the bitmap to the GPIO pins once per period. The
 * per-bit helpers bspDoutSetBit/bspDoutGetBit are internal implementation
 * details. */

void bspDoutSetBitmap(uint64_t mask, bool state);
uint64_t bspDoutGetBitmap(void);
void bspDoutUpdate(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_DIO_H */
