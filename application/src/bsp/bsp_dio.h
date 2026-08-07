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
	/* Trolley enable */
	DOUT_TROLLEY_ENABLE_DRV    = 0,   /* PH1  */

	/* External watchdog (MAX6703A WDI) */
	DOUT_WDI                   = 1,   /* PH9  */

	/* Power good / K13 enable */
	DOUT_PG_13V5               = 2,   /* PA12 */
	DOUT_K13_EN                = 3,   /* PB6  */

	/* Debug LEDs */
	DOUT_DBG_LED0              = 4,   /* PC8  */
	DOUT_DBG_LED1              = 5,   /* PC9  */
	DOUT_DBG_LED2              = 6,   /* PC10 */

	/* Panel LED indicators
	 * Note: PD8 (led_pac230v_on) / PD9 (led_s1_sys_on) removed — USART3 */
	DOUT_LED_PWR_24_ON         = 7,   /* PD0  */
	DOUT_LED_CP_224V_ON        = 8,   /* PD1  */
	DOUT_LED_GRID_PWR_IN       = 9,   /* PD2  */
	DOUT_LED_UPS_IN            = 10,  /* PD3  */
	DOUT_LED_SYSTEM_ON         = 11,  /* PD4  */
	DOUT_LED_S2_SOLO_SYS       = 12,  /* PD5  */
	DOUT_LED_TROLLEY_CONNECTED = 13,  /* PD6  */
	DOUT_LED_IS_PC_ON          = 14,  /* PD7  */
	DOUT_LED_S2_SYS_ON         = 15,  /* PD10 */
	DOUT_LED_APP_HOST_ON       = 16,  /* PD12 */

	/* Relay / power drivers (K2..K13) */
	DOUT_K5_DRV                = 17,  /* PI4  */
	DOUT_K6_DRV                = 18,  /* PI5  */
	DOUT_K3_DRV                = 19,  /* PI6  */
	DOUT_K2_DRV                = 20,  /* PI7  */
	DOUT_K4_DRV                = 21,  /* PI8  */
	DOUT_K7_DRV                = 22,  /* PI9  */
	DOUT_K10_EN                = 23,  /* PI10 */
	DOUT_K11_EN                = 24,  /* PI11 */
	DOUT_K12_EN                = 25,  /* PI12 */
	DOUT_K8_1_EN               = 26,  /* PI13 */
	DOUT_K8_2_EN               = 27,  /* PI14 */
	DOUT_K9_EN                 = 28,  /* PI15 */

	/* Status output drivers */
	DOUT_DRV_IS_PC_SITE_ON     = 29,  /* PJ11 */
	DOUT_DRV_APP_HOST_SITE_ON  = 30,  /* PJ12 */
	DOUT_MAINS_CONNECTED_IS_PC = 31,  /* PJ13 */
	DOUT_MAINS_CONNECTED_MCU   = 32,  /* PJ14 */
};

/* ==================== DIN index — pin_config.xlsx ==================== */

enum {
	DIN_GRID_MAIN_RELAY_STATUS  = 0,   /* PH5  — high=valid, low=invalid */
	DIN_ME_BOX_ERROR            = 1,   /* PH6  — high=normal, low=fault */
	DIN_FAULT0                  = 2,   /* PA8  */
	DIN_FAULT1                  = 3,   /* PA9  */
	DIN_FAULT2                  = 4,   /* PA10 */
	DIN_FAULT3                  = 5,   /* PA11 */
	DIN_TEMP_ALERT              = 6,   /* PB12 */
	DIN_LED_PWR_24_ON           = 7,   /* PC6  */
	DIN_FAULT4                  = 8,   /* PC11 */
	DIN_FAULT5                  = 9,   /* PC12 */
	DIN_FAULT6                  = 10,  /* PC13 */
	DIN_TRL_MU_CONNECTED_MCU    = 11,  /* PD13 */
	DIN_TRL_MU_CONNECTED_IS_PC  = 12,  /* PD14 */
	DIN_SYSTEM_ON_OFF           = 13,  /* PJ0  */
	DIN_SYSTEM_RESET            = 14,  /* PJ1  */
	DIN_S1_SYSTEM_CONFIG        = 15,  /* PJ2  */
	DIN_S2_SYSTEM_CONFIG        = 16,  /* PJ3  */
	DIN_SOLO_SYSTEM_CONFIG      = 17,  /* PJ4  */
	DIN_TROLLEY_CONNECTED       = 18,  /* PJ5  — high=connected */
	DIN_APP_HOST_ON             = 19,  /* PJ7  */
	DIN_SMART_WHS_INDICATE      = 20,  /* PJ8  */
	DIN_DRAWER_INDICATE         = 21,  /* PJ9  */
	DIN_SMART_CTRL_WHS_SEARCH   = 22,  /* PJ10 */
	DIN_IS_PC_ON                = 23,  /* PJ15 */
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
 * State machine drives the DOUT bitmap via bspDoutSetBit(pin, state) and
 * bspDoutSetBitmap(mask, state); bspDoutUpdate() (called from the
 * scheduler) applies the bitmap to the GPIO pins once per period.
 * bspDoutUpdate() stays for immediate writes (init, safety shutdown). */

void bspDoutSetBit(uint8_t pin, bool state);
bool bspDoutGetBit(uint8_t pin);
void bspDoutSetBitmap(uint32_t mask, bool state);
uint32_t bspDoutGetBitmap(void);
void bspDoutUpdate(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_DIO_H */
