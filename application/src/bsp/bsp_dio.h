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

#include <zephyr/sys/util.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== DOUT index — pin_config.xlsx ==================== */

enum {
	/* Relay / power drivers */
	DOUT_TROLLEY_ENABLE_DRV    = 0,   /* PH1  */
	DOUT_K3_1_DRV			    = 1, /* PK1  */
	DOUT_K3_2_DRV			    = 2, /* PK2  */
	DOUT_K4_DRV			    = 3, /* PK3  */
	DOUT_K5_DRV			    = 4, /* PI4  */
	DOUT_K6_DRV			    = 5, /* PI5  */
	DOUT_K8_1_DRV			    = 6, /* PI6  */
	DOUT_K8_2_DRV			    = 7, /* PI7  */
	DOUT_K9_DRV			    = 8, /* PI9  */
	DOUT_K10_DRV			    = 9, /* PI10 */
	DOUT_K11_DRV			    = 10, /* PI11 */
	DOUT_K12_DRV			    = 11, /* PI12 */

	/* Panel LED indicators */
	DOUT_LED_S1_SYS_ON			    = 12, /* PB10 */
	DOUT_LED_S2_SYS_ON			    = 13, /* PB11 */
	DOUT_DBG_LED0			    = 14, /* PC8  */
	DOUT_DBG_LED1			    = 15, /* PC9  */
	DOUT_DBG_LED2			    = 16, /* PC10 */
	DOUT_LED_PAC230V_ON			    = 17, /* PC15 */
	DOUT_LED_GRID_PWR_IN			    = 18, /* PD2  */
	DOUT_LED_UPS_IN			    = 19, /* PD3  */
	DOUT_LED_SYSTEM_ON			    = 20, /* PD4  */
	DOUT_LED_S2_SOLO_SYS			    = 21, /* PD5  */
	DOUT_LED_TROLLEY_CONNECTED			    = 22, /* PD6  */
	DOUT_LED_IS_PC_ON			    = 23, /* PD7  */
	DOUT_LED_APPHOST_ON			    = 24, /* PD11 */

	/* Status output drivers */
	DOUT_TROLLEY_CONNECTED_MCU			    = 25, /* PI13 */
	DOUT_TROLLEY_CONNECTED_IS_PC			    = 26, /* PI14 */
	DOUT_DRV_IS_PC_SITE_ON			    = 27, /* PJ11 */
	DOUT_DRV_APP_HOST_SITE_ON			    = 28, /* PJ12 */
	DOUT_MAINS_CONNECTED_APPHOST		    = 29, /* PJ13 */
	DOUT_MAINS_CONNECTED_IS_PC			    = 30, /* PJ14 */

	/* External watchdog (MAX6703A WDI) */
	DOUT_WDI			    = 31, /* PH9 */

};

/* ==================== DIN index — pin_config.xlsx ==================== */

enum {
	DIN_GRID_MAIN_RELAY_STATUS  = 0,   /* PH5  — high=valid, low=invalid */
	DIN_ME_BOX_ERROR            = 1,   /* PH6  — high=normal, low=fault */
	DIN_TROLLEY_CONNECTED			= 2,   /* PA0  — high=connected, low=disconnected */
	DIN_TEMP_ALERT			= 3,   /* PB12 */
	DIN_LED_PWR_24_ON			= 4,   /* PC6  */
	DIN_LED_CP_24V_ON			= 5,   /* PC7  */
	DIN_SYSTEM_ON_OFF			= 6,   /* PJ0  */
	DIN_SYSTEM_RESET			= 7,   /* PJ1  */
	DIN_S1_SYSTEM_CONFIG			= 8,   /* PJ2  */
	DIN_S2_SYSTEM_CONFIG			= 9,   /* PJ3  */
	DIN_SOLO_SYSTEM_CONFIG		= 10,  /* PJ4  */
	DIN_TROLLEY_CONNECTED_J		= 11,  /* PJ5  — high=connected */
	DIN_IS_PC_ON			= 12,  /* PJ6  */
	DIN_APP_HOST_ON			= 13,  /* PJ7  */
	DIN_SMART_WHS_INDICATE		= 14,  /* PJ8  */
	DIN_DRAWER_INDICATE			= 15,  /* PJ9  */
	DIN_SMART_CTRL_WHS_SEARCH	= 16,  /* PJ10 */
};

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
 * State machine sets a DOUT bit via bspDoutSetStatus(pin, state);
 * bspDoutSet() (called from the scheduler) applies the bitmap to
 * the GPIO pins once per period. bspDoutSet() stays for immediate
 * writes (init, safety shutdown). */

void bspDoutSetStatus(uint8_t pin, bool state);
bool bspDoutGetStatus(uint8_t pin);
void bspDoutSetMask(uint32_t mask, bool state);
void bspDoutSet(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_DIO_H */
