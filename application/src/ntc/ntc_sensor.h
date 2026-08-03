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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AIN channels for NTC sensors (matches bsp_ain.h enum) */
#define AIN_TEMP1  5   /* AIN_ADC_TEMP1 — PA3 ADC1_INP15 */
#define AIN_TEMP2  6   /* AIN_ADC_TEMP2 — PA4 ADC1_INP18 */

/* Temperature thresholds (×10, i.e. 25.0°C = 250) */
#define NTC_TEMP_WARN    750    /* 75°C — warning             */
#define NTC_TEMP_FAULT   900    /* 90°C — fault shutdown      */
#define NTC_TEMP_FAN_MAX 500    /* 50°C — max fan speed       */
#define NTC_TEMP_FAN_MID 300    /* 30°C — medium fan speed    */

/*
 * Read NTC temperature via ADC channel.
 *
 * channel: AIN channel index (AIN_TEMP1 or AIN_TEMP2)
 *
 * Returns temperature × 10 (e.g., 250 = 25.0°C), or INT16_MIN on error.
 */
int16_t ntcReadTemp(uint8_t ain_channel);

#ifdef __cplusplus
}
#endif

#endif /* NTC_SENSOR_H */
