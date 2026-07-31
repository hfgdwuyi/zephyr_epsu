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
	DOUT_PWR_ON_OFF            = 1,   /* PA5  */
	DOUT_K3_1_DRV              = 2,   /* PK1  */
	DOUT_K3_2_DRV              = 3,   /* PK2  */
	DOUT_K4_DRV                = 4,   /* PK3  */
	DOUT_K5_DRV                = 5,   /* PI4  */
	DOUT_K6_DRV                = 6,   /* PI5  */
	DOUT_K8_1_DRV              = 7,   /* PI6  */
	DOUT_K8_2_DRV              = 8,   /* PI7  */
	DOUT_K9_DRV                = 9,   /* PI9  */
	DOUT_K10_DRV               = 10,  /* PI10 */
	DOUT_K11_DRV               = 11,  /* PI11 */
	DOUT_K12_DRV               = 12,  /* PI12 */

	/* Panel LED indicators */
	DOUT_LED_S1_SYS_ON         = 13,  /* PB10 */
	DOUT_LED_S2_SYS_ON         = 14,  /* PB11 */
	DOUT_DBG_LED0              = 15,  /* PC8  */
	DOUT_DBG_LED1              = 16,  /* PC9  */
	DOUT_DBG_LED2              = 17,  /* PC10 */
	DOUT_LED_PAC230V_ON        = 18,  /* PC15 */
	DOUT_LED_GRID_PWR_IN       = 19,  /* PD2  */
	DOUT_LED_UPS_IN            = 20,  /* PD3  */
	DOUT_LED_SYSTEM_ON         = 21,  /* PD4  */
	DOUT_LED_S2_SOLO_SYS       = 22,  /* PD5  */
	DOUT_LED_TROLLEY_CONNECTED = 23,  /* PD6  */
	DOUT_LED_IS_PC_ON          = 24,  /* PD7  */
	DOUT_LED_APPHOST_ON        = 25,  /* PD11 */

	/* Status output drivers */
	DOUT_TROLLEY_CONNECTED_MCU  = 26, /* PI13 */
	DOUT_TROLLEY_CONNECTED_IS_PC= 27, /* PI14 */
	DOUT_DRV_IS_PC_SITE_ON      = 28, /* PJ11 */
	DOUT_DRV_APP_HOST_SITE_ON   = 29, /* PJ12 */
	DOUT_MAINS_CONNECTED_APPHOST= 30, /* PJ13 */
	DOUT_MAINS_CONNECTED_IS_PC  = 31, /* PJ14 */

	/* External watchdog (MAX6703A WDI) */
	DOUT_WDI                    = 32, /* PH9 */

};

/* ==================== DIN index — pin_config.xlsx ==================== */

enum {
	DIN_GRID_MAIN_RELAY_STATUS  = 0,  /* PH5  */
	DIN_ME_BOX_ERROR            = 1,  /* PH6  */
	DIN_TROLLEY_CONNECTED       = 2,  /* PA0  */
	DIN_TEMP_ALERT              = 3,  /* PB12 */
	DIN_LED_PWR_24_ON           = 4,  /* PC6  */
	DIN_LED_CP_24V_ON           = 5,  /* PC7  */
	DIN_SYSTEM_ON_OFF           = 6,  /* PJ0  */
	DIN_SYSTEM_RESET            = 7,  /* PJ1  */
	DIN_S1_SYSTEM_CONFIG        = 8,  /* PJ2  */
	DIN_S2_SYSTEM_CONFIG        = 9,  /* PJ3  */
	DIN_SOLO_SYSTEM_CONFIG      = 10, /* PJ4  */
	DIN_TROLLEY_CONNECTED_J     = 11, /* PJ5  */
	DIN_IS_PC_ON                = 12, /* PJ6  */
	DIN_APP_HOST_ON             = 13, /* PJ7  */
	DIN_SMART_WHS_INDICATE      = 14, /* PJ8  */
	DIN_DRAWER_INDICATE         = 15, /* PJ9  */
	DIN_SMART_CTRL_WHS_SEARCH   = 16, /* PJ10 */
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
} bspDinSettings;

extern const uint8_t dinMax;
extern const uint8_t doutMax;

/* ==================== Public API ==================== */

void bspDioInit(void);

/* Configure debouncing for a DIN pin index */
void bspDinSetDebouncing(uint8_t pin, bspDinSettings settings);

/* Read one packed DIN byte (byte index [0..DIN_BYTES-1]) */
uint8_t bspDinRead(uint8_t byte);

/* Read a single DIN pin state by index (e.g., DIN_GRID_MAIN_RELAY_STATUS) */
static inline bool bspDinGet(uint8_t pin)
{
	return (bspDinRead((pin) / 8U) & BIT((pin) % 8U)) != 0U;
}

/* Set/read DOUT by pin index */
void bspDoutSet(uint8_t pinNumber, bool state);
bool bspDoutRead(uint8_t pinNumber);

/* Periodic DOUT refresh — mirrors input states to status output drivers only */
void bspDoutUpdate(void);

/* MAX6703A WDI toggle — must be called within 1.6s timeout */
void bspWdiFeed(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_DIO_H */
