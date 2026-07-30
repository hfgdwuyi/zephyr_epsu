/*
 * ntc_sensor.h
 *
 * NTC thermistor temperature conversion — cios-zhong.
 * Vishay NTCALUG02A472FA: R25=4700Ω, B(25/85)=3984K.
 *
 * Voltage divider: Vref=3.3V, Rfixed=4700Ω (Vcc—Rf—ADC—NTC—GND).
 *
 * Depends on: bsp_ain.h (ADC raw values), NOT a BSP module itself.
 */

#ifndef NTC_SENSOR_H
#define NTC_SENSOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AIN channel indices for NTC sensors (matches app.overlay io-channels) */
#define AIN_TEMP1       5   /* PA3 - ADC1_INP15, adc_temp1 */
#define AIN_TEMP2       6   /* PA4 - ADC1_INP18, adc_temp2 */

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
int16_t ntc_read_temp(uint8_t ain_channel);

#ifdef __cplusplus
}
#endif

#endif /* NTC_SENSOR_H */
