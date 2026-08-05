/*
 * ntc_sensor.h
 *
 * NTC thermistor temperature conversion — cios-zhong.
 * Vishay NTCALUG02A472FA: R25=4700Ω, B(25/85)=3984K.
 *
 * Voltage divider: Vref=3.3V, Rfixed=4700Ω (Vcc—Rf—ADC—NTC—GND).
 *
 * Depends on: bsp_ain.h (ADC/channel definitions)
 */

#ifndef NTC_SENSOR_H
#define NTC_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AIN channels for NTC sensors (matches bsp_ain.h enum) */
typedef enum {
	NTC_AIN_CH_TEMP1 = 5,   /* AIN_ADC_TEMP1 — PA3 ADC1_INP15 */
	NTC_AIN_CH_TEMP2 = 6,   /* AIN_ADC_TEMP2 — PA4 ADC1_INP18 */
} ntcAinCh_t;

/* Temperature thresholds (×10, i.e. 25.0°C = 250) */
typedef enum {
	NTC_TEMP_THRESH_WARN     = 750,   /* 75°C — warning             */
	NTC_TEMP_THRESH_FAULT    = 900,   /* 90°C — fault shutdown      */
	NTC_TEMP_THRESH_FAN_MAX  = 500,   /* 50°C — max fan speed       */
	NTC_TEMP_THRESH_FAN_MID  = 300,   /* 30°C — medium fan speed    */
} ntcTempThresh_t;

/*
 * Read NTC temperature via ADC channel.
 *
 * channel: AIN channel index (NTC_AIN_CH_TEMP1 or NTC_AIN_CH_TEMP2)
 *
 * Returns temperature × 10 (e.g., 250 = 25.0°C), or INT16_MIN on error.
 */
int16_t ntcReadTemp(uint8_t ain_channel);

/* ---- Temperature monitoring service ----
 * Manages sampling of both NTC channels plus over-temp accumulation.
 * The state machine calls ntcTempUpdate() periodically (e.g. every 1 s)
 * and queries the result. Fan-speed policy stays in the state machine. */

void     ntcTempInit(void);       /* reset internal state */
void     ntcTempUpdate(uint32_t period_ms); /* sample both channels; period_ms drives over-temp counter */
int16_t  ntcTempGetMax(void);     /* hotter of the two temps (×10), or <= 0 if bad */
bool     ntcTempIsOvertemp(void); /* max temp >= fault threshold */
uint32_t ntcTempOvertempFor(void);/* consecutive over-temp ms (for fault decision) */

#ifdef __cplusplus
}
#endif

#endif /* NTC_SENSOR_H */
