/*
 * sensor.h
 *
 * ADC-based sensor interpretation — cios-zhong (application layer).
 *
 * All engineering-unit conversion of AIN samples lives here. The BSP
 * (bsp_ain) only exposes raw 12-bit ADC counts, refreshed on a fixed poll
 * cadence into a per-channel snapshot array.
 *
 * The sensor module is driven by a dedicated thread owned by the scheduler
 * (scheduler.c). sensorUpdate() is that thread's periodic tick: it reads the
 * raw snapshot, low-pass filters each channel, then converts and publishes
 * physical values into a cache. Each physical quantity is processed at its
 * own rate (multi-rate decimation), so fast signals (PDC rails) update at
 * the base tick while slow signals (NTC temperature) update less often.
 *
 * Consumers (state machine, terminal task) read the published values through
 * sensorGetPhys() / sensorTempGet*().
 *
 *   Temperature — NTC thermistor (Vishay NTCALUG02A472FA: R25=4700Ω,
 *                 B(25/85)=3984K) via lookup table + linear interpolation.
 *   Voltage     — PDC divider (47k:4.7k). Mains AC (AIN_ADC_VIN) is measured
 *                 by the ac_meter module (windowed RMS), not here.
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
 * One-shot live read of an NTC channel (raw → temperature).
 *
 * Returns temperature × 10 (e.g., 250 = 25.0°C), or INT16_MIN on error
 * (ADC not ready, open/short). For continuous monitoring prefer the cache
 * via the sensor thread and sensorTempGet*().
 */
int16_t sensorReadTemp(uint8_t ain_channel);

/* ---- Temperature monitoring service ----
 * Samples are captured by the sensor thread at the temperature channel rate
 * (1 s). Over-temp duration is accumulated there as well, so consumers only
 * query. Values are shared across threads via volatile state. */

void     sensorTempInit(void);         /* reset internal state */
int16_t  sensorTempGet1(void);         /* temp sensor 1 (×10), INT16_MIN on fault */
int16_t  sensorTempGet2(void);         /* temp sensor 2 (×10), INT16_MIN on fault */
int16_t  sensorTempGetMax(void);       /* hotter of the two temps (×10), or <= 0 if bad */
bool     sensorTempSensorFault(void);  /* true if either NTC sensor read failed */
bool     sensorTempIsOvertemp(void);   /* max temp >= fault threshold */
uint32_t sensorTempOvertempFor(void);  /* consecutive over-temp ms (for fault decision) */

/* ==================== Sensor processing ==================== */

/* Sensor thread cadence — the scheduler calls sensorUpdate() every base tick. */
#define SENSOR_BASE_PERIOD_MS  50

/*
 * Process one base tick: read the raw ADC snapshot, IIR-filter each
 * configured channel, and publish physical values at each channel's rate
 * (multi-rate decimation). Also accumulates over-temp duration. Called by
 * the scheduler's sensor thread.
 */
void sensorUpdate(void);

/*
 * Latest published physical value for an AIN channel.
 *
 * Units depend on the channel: voltage channels return mV, NTC channels
 * return temperature ×10 (int16 bit-pattern; check sensorTempGet*() for a
 * typed read). Returns 0 if the channel has never been published.
 */
uint32_t sensorGetPhys(uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_H */
