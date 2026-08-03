/*
 * bsp_ain.h — Analog Input (Zephyr port) — cios-zhong
 */

#ifndef BSP_AIN_H
#define BSP_AIN_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AIN logical channels (matches app.overlay io-channels order) */

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
	AIN_ADC_VIN,            /* 11: PC2   — mains AC, special divider     */
	AIN_ADC_PDC0_ALT,       /* 12: PC3   — PSU output, 47k:4.7k divider */
	AIN_ADC_5V,             /* 13: PH4   — 5V monitor, direct            */
};

/*
 * Voltage divider constants (all PDCx channels).
 * rHigh/rLow in units of 0.1kΩ: 47kΩ / 4.7kΩ.
 * Vactual = Vadc × (470 + 47) / 47 = Vadc × 11
 * 24 V → 2.4 V at pin;  range 19.2 – 28.8 V
 */
#define AIN_DIV_RHIGH        470
#define AIN_DIV_RLOW          47

/* PDC validity (24 V PSU output ±20%) */
#define AIN_PDC_VALID_MIN   19200   /* 19.2 V */
#define AIN_PDC_VALID_MAX   28800   /* 28.8 V */

/* ==================== API ==================== */

void     bspAinInit(void);
void     bspAinPoll(void);

/* Raw ADC value (12-bit, 0–4095) */
uint32_t bspAinGetRawValue(uint8_t channel);

/* ADC pin voltage in millivolts (0–3300 mV) */
uint32_t bspAinReadMv(uint8_t channel);

/*
 * Actual voltage behind a resistor divider, in mV.
 * rHigh / rLow in 0.1kΩ units.  E.g. PDC: bspAinReadDivMv(ch, 470, 47)
 * returns the PSU output voltage in mV.
 */
uint32_t bspAinReadDivMv(uint8_t channel, uint32_t rHigh, uint32_t rLow);

/*
 * Mains AC voltage at AIN_ADC_VIN, result in mV.
 * Vadc = 1.65 + (0.4 x 1.414 x Vin x 50.2 / 3575) x 3.9 / 6.49
 *   => Vin = (Vadc - 1.65) / 0.004773
 */
uint32_t bspAinReadVinMv(void);

/*
 * Convenience: PDCx channel through standard divider → mV.
 * Equivalent to bspAinReadDivMv(ch, AIN_DIV_RHIGH, AIN_DIV_RLOW).
 */
static inline uint32_t bspAinReadPdcMv(uint8_t channel)
{
	return bspAinReadDivMv(channel, AIN_DIV_RHIGH, AIN_DIV_RLOW);
}

static inline bool bspAinPdcValid(uint32_t mv)
{
	return mv >= AIN_PDC_VALID_MIN && mv <= AIN_PDC_VALID_MAX;
}

const char *bspAinGetName(uint8_t channel);
extern uint8_t AIN_NUMBER;

#ifdef __cplusplus
}
#endif

#endif /* BSP_AIN_H */
