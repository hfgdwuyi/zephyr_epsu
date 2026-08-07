/*
 * ain.h — AIN application task (circuit-config scaling)
 *
 * bsp_ain only exposes raw 12-bit ADC samples. This layer applies the
 * external circuit configuration — PDC voltage dividers (47k:4.7k), the
 * mains-AC sense network — and presents engineering units in mV.
 */

#ifndef AIN_H
#define AIN_H

#include <stdbool.h>
#include <stdint.h>

/* Channel indices (AIN_ADC_*) and raw access live in bsp_ain.h. */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PDC divider constants (all PDCx channels).
 * rHigh/rLow in 0.1kΩ units: 47kΩ / 4.7kΩ.
 * Vactual = Vadc × (470 + 47) / 47 = Vadc × 11
 * 24 V → 2.4 V at pin; range 19.2 – 28.8 V
 */
typedef enum {
	AIN_DIV_RHIGH       = 470,
	AIN_DIV_RLOW        = 47,
} ainDivider_t;

/* PDC validity (24 V PSU output ±20%) */
typedef enum {
	AIN_PDC_VALID_MIN   = 19200,   /* 19.2 V */
	AIN_PDC_VALID_MAX   = 28800,   /* 28.8 V */
} ainPdcRange_t;

/* ==================== API ==================== */

/* Drive one sampling round (wraps bspAinPoll). Call from the scheduler. */
void ainPoll(void);

/* ADC pin voltage in millivolts (0–3300 mV) */
uint32_t ainReadMv(uint8_t channel);

/*
 * Actual voltage behind a resistor divider, in mV.
 * rHigh / rLow in 0.1kΩ units.  E.g. PDC: ainReadDivMv(ch, 470, 47)
 * returns the PSU output voltage in mV.
 */
uint32_t ainReadDivMv(uint8_t channel, uint32_t rHigh, uint32_t rLow);

/*
 * Mains AC voltage at AIN_ADC_VIN, result in mV.
 * Vadc = 1.65 + (0.4 x 1.414 x Vin x 50.2 / 3575) x 3.9 / 6.49
 *   => Vin = (Vadc - 1.65) / 0.004773
 */
uint32_t ainReadVinMv(void);

/*
 * Convenience: PDCx channel through standard divider → mV.
 * Equivalent to ainReadDivMv(ch, AIN_DIV_RHIGH, AIN_DIV_RLOW).
 */
static inline uint32_t ainReadPdcMv(uint8_t channel)
{
	return ainReadDivMv(channel, AIN_DIV_RHIGH, AIN_DIV_RLOW);
}

static inline bool ainPdcValid(uint32_t mv)
{
	return mv >= AIN_PDC_VALID_MIN && mv <= AIN_PDC_VALID_MAX;
}

#ifdef __cplusplus
}
#endif

#endif /* AIN_H */
