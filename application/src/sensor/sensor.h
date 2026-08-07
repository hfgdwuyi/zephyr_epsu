/*
 * sensor.h
 *
 * ADC-based sensor interpretation — cios-zhong (application layer).
 *
 * All engineering-unit conversion of AIN samples lives here. The BSP
 * (bsp_ain) only exposes raw 12-bit ADC counts.
 *
 *   Temperature — NTC thermistor (Vishay NTCALUG02A472FA: R25=4700Ω,
 *                 B(25/85)=3984K) via lookup table + linear interpolation.
 *   Voltage     — PDC divider (47k:4.7k) and mains AC sense network.
 *
 * Depends on: bsp_ain.h (ADC/channel definitions)
 */

#ifndef SENSOR_H
#define SENSOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Temperature (NTC) ==================== */

/* AIN channels for NTC sensors (matches bsp_ain.h enum) */
typedef enum {
	SENSOR_AIN_CH_TEMP1 = 5,   /* AIN_ADC_TEMP1 — PA3 ADC1_INP15 */
	SENSOR_AIN_CH_TEMP2 = 6,   /* AIN_ADC_TEMP2 — PA4 ADC1_INP18 */
} sensorAinCh_t;

/* Temperature thresholds (×10, i.e. 25.0°C = 250) */
typedef enum {
	SENSOR_TEMP_THRESH_WARN     = 750,   /* 75°C — warning             */
	SENSOR_TEMP_THRESH_FAULT    = 900,   /* 90°C — fault shutdown      */
	SENSOR_TEMP_THRESH_FAN_MAX  = 500,   /* 50°C — max fan speed       */
	SENSOR_TEMP_THRESH_FAN_MID  = 300,   /* 30°C — medium fan speed    */
} sensorTempThresh_t;

/*
 * Read NTC temperature via ADC channel.
 *
 * channel: AIN channel index (SENSOR_AIN_CH_TEMP1 or SENSOR_AIN_CH_TEMP2)
 *
 * Returns temperature × 10 (e.g., 250 = 25.0°C), or INT16_MIN on error.
 */
int16_t sensorReadTemp(uint8_t ain_channel);

/* ---- Temperature monitoring service ----
 * Manages sampling of both NTC channels plus over-temp accumulation.
 * The state machine calls sensorTempUpdate() periodically (e.g. every 1 s)
 * and queries the result. Fan-speed policy stays in the state machine. */

void     sensorTempInit(void);        /* reset internal state */
void     sensorTempUpdate(uint32_t period_ms); /* sample both channels; period_ms drives over-temp counter */
int16_t  sensorTempGetMax(void);      /* hotter of the two temps (×10), or <= 0 if bad */
bool     sensorTempSensorFault(void); /* true if either NTC sensor read failed */
bool     sensorTempIsOvertemp(void);  /* max temp >= fault threshold */
uint32_t sensorTempOvertempFor(void); /* consecutive over-temp ms (for fault decision) */

/* ==================== Voltage (dividers / mains AC) ==================== */

/*
 * PDC divider constants (all PDCx channels).
 * rHigh/rLow in 0.1kΩ units: 47kΩ / 4.7kΩ.
 * Vactual = Vadc × (470 + 47) / 47 = Vadc × 11
 * 24 V → 2.4 V at pin;  range 19.2 – 28.8 V
 */
typedef enum {
	SENSOR_DIV_RHIGH       = 470,
	SENSOR_DIV_RLOW        = 47,
} sensorDivider_t;

/* PDC validity (24 V PSU output ±20%) */
typedef enum {
	SENSOR_PDC_VALID_MIN   = 19200,   /* 19.2 V */
	SENSOR_PDC_VALID_MAX   = 28800,   /* 28.8 V */
} sensorPdcRange_t;

/* ADC pin voltage in millivolts (0–3300 mV) */
uint32_t sensorReadMv(uint8_t channel);

/*
 * Actual voltage behind a resistor divider, in mV.
 * rHigh / rLow in 0.1kΩ units.  E.g. PDC: sensorReadDivMv(ch, 470, 47)
 * returns the PSU output voltage in mV.
 */
uint32_t sensorReadDivMv(uint8_t channel, uint32_t rHigh, uint32_t rLow);

/*
 * Mains AC voltage at AIN_ADC_VIN, result in mV.
 * Vadc = 1.65 + (0.4 x 1.414 x Vin x 50.2 / 3575) x 3.9 / 6.49
 *   => Vin = (Vadc - 1.65) / 0.004773
 */
uint32_t sensorReadVinMv(void);

/*
 * Convenience: PDCx channel through standard divider → mV.
 * Equivalent to sensorReadDivMv(ch, SENSOR_DIV_RHIGH, SENSOR_DIV_RLOW).
 */
static inline uint32_t sensorReadPdcMv(uint8_t channel)
{
	return sensorReadDivMv(channel, SENSOR_DIV_RHIGH, SENSOR_DIV_RLOW);
}

static inline bool sensorPdcValid(uint32_t mv)
{
	return mv >= SENSOR_PDC_VALID_MIN && mv <= SENSOR_PDC_VALID_MAX;
}

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_H */
