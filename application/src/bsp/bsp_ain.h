/*
 * bsp_ain.h — Analog Input (Zephyr port) — cios-zhong
 */

#ifndef BSP_AIN_H
#define BSP_AIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== AIN channel index (matches app.overlay io-channels) ==================== */

enum {
	AIN_ADC_3V3,            /*  0: PF6   — 3.3V monitor, direct         */
	AIN_ADC_PDC1,           /*  1: PF7   — PSU output, 47k:4.7k divider */
	AIN_ADC_PDC7,           /*  2: PF8   — PSU output, 47k:4.7k divider */
	AIN_ADC_PDC6,           /*  3: PF9   — PSU output, 47k:4.7k divider */
	AIN_ADC_PDC5,           /*  4: PF10  — PSU output, 47k:4.7k divider */
	AIN_ADC_TEMP1,          /*  5: PA3   — NTC sensor 1 (no divider)    */
	AIN_ADC_TEMP2,          /*  6: PA4   — NTC sensor 2 (no divider)    */
	AIN_ADC_PDC0,           /*  7: PA6   — PSU output, 47k:4.7k divider */
	AIN_ADC_PDC4,           /*  8: PC0   — PSU output, 47k:4.7k divider */
	AIN_ADC_PDC2,           /*  9: PB0   — PSU output, 47k:4.7k divider */
	AIN_ADC_PDC3,           /* 10: PB1   — PSU output, 47k:4.7k divider */
	AIN_ADC_VIN,            /* 11: PC2   — input voltage, 47k:4.7k div  */
	AIN_ADC_PDC0_ALT,       /* 12: PC3   — PSU output, 47k:4.7k divider */
	AIN_ADC_5V,             /* 13: PH4   — 5V monitor, direct            */
};

/* ---- Voltage divider (all PDCx + VIN channels) ---- */
#define AIN_DIV_RHIGH        470    /* ×0.1kΩ: 47kΩ                     */
#define AIN_DIV_RLOW          47    /* ×0.1kΩ: 4.7kΩ                    */
/* Vactual = Vadc × (470 + 47) / 47 = Vadc × 11                         */
/* Nominal range: 19.2 V – 28.8 V (24 V PSU output)                     */

/* ==================== API ==================== */

void     bspAinInit(void);
void     bspAinPoll(void);

/* Raw ADC value (12-bit, 0–4095) */
uint32_t bspAinGetRawValue(uint8_t channel);

/* ADC pin voltage in millivolts (0–3300 mV) */
uint32_t bspAinReadMv(uint8_t channel);

/*
 * Actual voltage behind a resistor divider, in mV.
 * rHigh / rLow in units of 0.1kΩ: 47k/4.7k → rHigh=470, rLow=47.
 */
uint32_t bspAinReadDivMv(uint8_t channel, uint32_t rHigh, uint32_t rLow);

const char *bspAinGetName(uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif /* BSP_AIN_H */
