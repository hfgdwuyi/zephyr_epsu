/*
 * sensor.c
 *
 * ADC-based sensor interpretation — application layer.
 *
 * Temperature (NTC): thermistor R→T conversion via lookup table + linear
 *   interpolation.  Vishay NTCALUG02A472FA: R25=4700Ω ±1%, B(25/85)=3984K.
 *   Voltage divider:  Vcc ─── Rf(4700Ω) ─── ADC ─── NTC ─── GND
 *   Vadc = Vref × Rntc / (Rf + Rntc)  ⇒  Rntc = Rf × Vadc / (Vref - Vadc)
 *   where Vref=3.3V, Rf=4700Ω, Vadc = raw × 3.3 / 4095
 *
 *   Also provides the temperature monitoring service (sensorTemp*): samples
 *   both NTC channels and accumulates over-temp duration for the state
 *   machine's fault decision.
 *
 * Voltage: external-circuit scaling for the PDC dividers (47k:4.7k) and the
 *   mains AC sense network.  bsp_ain only exposes raw ADC counts.
 */

/* C standard library */
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

/* BSP */
#include "bsp_ain.h"

/* Application */
#include "sensor.h"

/* ==================== Temperature (NTC) ==================== */

/* ─── voltage divider params ─── */
typedef enum {
	NTC_HW_VREF_MV     = 3300,
	NTC_HW_RFIXED_OHM  = 4700,
	NTC_HW_ADC_BITS    = 12,
	NTC_HW_ADC_MAX     = (1U << NTC_HW_ADC_BITS) - 1,   /* 4095 */
} ntcHwParam_t;

/* ─── lookup table entry ─── */
typedef struct {
	int16_t  temp;      /* temperature × 10 (°C)     */
	uint32_t r_ohm;     /* NTC resistance at this T  */
} ntc_point_t;

/*
 * Sorted by temperature ascending (i.e. resistance descending).
 * Table condensed to every 5°C below 0°C, every 1°C above.
 * Interpolation provides sub-1°C resolution.
 */
static const ntc_point_t ntc_table[] = {
	/*   T(°C)   Rnom(Ω)     */
	{   -550,  448274 }, /* -55.0°C   */
	{   -500,  312160 }, /* -50.0°C   */
	{   -450,  220131 }, /* -45.0°C   */
	{   -400,  157109 }, /* -40.0°C   */
	{   -350,  113422 }, /* -35.0°C   */
	{   -300,   82782 }, /* -30.0°C   */
	{   -250,   61053 }, /* -25.0°C   */
	{   -200,   45478 }, /* -20.0°C   */
	{   -150,   34199 }, /* -15.0°C   */
	{   -100,   25953 }, /* -10.0°C   */
	{    -50,   19866 }, /* -5.0°C   */
	{      0,   15333 }, /*  0.0°C   */
	{     50,   11929 }, /*  5.0°C   */
	{    100,    9352 }, /*  10.0°C   */
	{    150,    7384 }, /*  15.0°C   */
	{    200,    5872 }, /*  20.0°C   */
	{    250,    4700 }, /*  25.0°C   */
	{    300,    3786 }, /*  30.0°C   */
	{    350,    3069 }, /*  35.0°C   */
	{    400,    2502 }, /*  40.0°C   */
	{    450,    2052 }, /*  45.0°C   */
	{    500,    1691 }, /*  50.0°C   */
	{    550,    1402 }, /*  55.0°C   */
	{    600,    1167 }, /*  60.0°C   */
	{    650,     977 }, /*  65.0°C   */
	{    700,     821 }, /*  70.0°C   */
	{    750,     694 }, /*  75.0°C   */
	{    800,     588 }, /*  80.0°C   */
	{    850,     501 }, /*  85.0°C   */
	{    900,     428 }, /*  90.0°C   */
	{    950,     368 }, /*  95.0°C   */
	{   1000,     317 }, /* 100.0°C   */
	{   1050,     274 }, /* 105.0°C   */
	{   1100,     238 }, /* 110.0°C   */
	{   1150,     207 }, /* 115.0°C   */
	{   1200,     181 }, /* 120.0°C   */
	{   1250,     158 }, /* 125.0°C   */
};

#define NTC_TABLE_LEN (sizeof(ntc_table) / sizeof(ntc_table[0]))

/* ─── linear interpolation ─── */

/*
 * Interpolate temperature (×10) for a given resistance between two
 * adjacent table points. Assumes r is between r_lo (colder, higher R)
 * and r_hi (warmer, lower R).
 */
static int16_t ntcInterpolate(uint32_t r_ohm)
{
	if (r_ohm >= ntc_table[0].r_ohm) {
		return ntc_table[0].temp;   /* at or below min temp */
	}
	if (r_ohm <= ntc_table[NTC_TABLE_LEN - 1].r_ohm) {
		return ntc_table[NTC_TABLE_LEN - 1].temp;  /* at or above max temp */
	}

	/* binary search: find the two entries that bracket r_ohm */
	/* table is sorted by temp ascending → R descending           */
	/* r_lo (higher R, lower T) has smaller index                */
	size_t lo = 0;
	size_t hi = NTC_TABLE_LEN - 1;

	while (hi - lo > 1) {
		size_t mid = (lo + hi) / 2;
		if (ntc_table[mid].r_ohm >= r_ohm) {
			lo = mid;
		} else {
			hi = mid;
		}
	}

	const ntc_point_t *pl = &ntc_table[lo];
	const ntc_point_t *ph = &ntc_table[hi];

	/* linear interpolation: T = Tlo + (Tdiff) × (Rlo - R) / (Rlo - Rhi) */
	uint32_t r_diff  = pl->r_ohm - ph->r_ohm;       /* Rlo > Rhi, positive */
	uint32_t r_delta = pl->r_ohm - r_ohm;           /* how far from Rlo  */
	int16_t  t_diff  = ph->temp - pl->temp;          /* Thi - Tlo, positive */

	/* T × 10 = Tlo×10 + t_diff×10 × r_delta / r_diff */
	int32_t num = (int32_t)t_diff * (int32_t)r_delta;
	int32_t interp = (int32_t)pl->temp + num / (int32_t)r_diff;

	return (int16_t)interp;
}

int16_t sensorReadTemp(uint8_t ain_channel)
{
	uint32_t raw = bspAinGetRawValue(ain_channel);

	if (raw == 0) {
		return INT16_MIN;   /* ADC not ready or read failed */
	}
	if (raw >= NTC_HW_ADC_MAX) {
		return INT16_MIN;   /* open / short to Vcc  */
	}

	/* Vadc(mV) = raw × Vref / ADCmax */
	uint32_t v_mv = (raw * NTC_HW_VREF_MV) / NTC_HW_ADC_MAX;

	/*
	 * Vadc = Vref × Rntc / (Rf + Rntc)
	 *   ⇒  Rntc = Rf × Vadc / (Vref - Vadc)
	 *
	 * All in mV, R in Ω.
	 */
	if (v_mv >= NTC_HW_VREF_MV) {
		return INT16_MIN;   /* NTC disconnected or short to Vref */
	}

	uint32_t v_drop_mv = NTC_HW_VREF_MV - v_mv;
	uint32_t r_ntc = (v_drop_mv == 0)
		? 0U
		: (uint32_t)((uint64_t)NTC_HW_RFIXED_OHM * v_mv / v_drop_mv);

	return ntcInterpolate(r_ntc);
}

/* ---- Temperature monitoring service ---- */

static int16_t  s_temp1;      /* temp sensor 1 (×10°C)     */
static int16_t  s_temp2;      /* temp sensor 2 (×10°C)     */
static uint32_t s_overtemp_ms; /* consecutive over-temp ms  */

void sensorTempInit(void)
{
	s_temp1 = 0;
	s_temp2 = 0;
	s_overtemp_ms = 0;
}

void sensorTempUpdate(uint32_t period_ms)
{
	s_temp1 = sensorReadTemp(SENSOR_AIN_CH_TEMP1);
	s_temp2 = sensorReadTemp(SENSOR_AIN_CH_TEMP2);

	/* Accumulate while over-temp, reset otherwise. period_ms lets the
	 * caller's poll period drive the over-temp counter in ms. */
	s_overtemp_ms = sensorTempIsOvertemp() ? s_overtemp_ms + period_ms : 0U;
}

int16_t sensorTempGetMax(void)
{
	return (s_temp1 > s_temp2) ? s_temp1 : s_temp2;
}

/* True if either NTC sensor read failed (open/short). sensorReadTemp returns
 * INT16_MIN on failure, so a negative sample means a sensor fault. */
bool sensorTempSensorFault(void)
{
	return (s_temp1 < 0) || (s_temp2 < 0);
}

bool sensorTempIsOvertemp(void)
{
	int16_t hi = sensorTempGetMax();
	return (hi > 0) && (hi >= SENSOR_TEMP_THRESH_FAULT);
}

uint32_t sensorTempOvertempFor(void)
{
	return s_overtemp_ms;
}

/* ==================== Voltage (dividers / mains AC) ==================== */

uint32_t sensorReadMv(uint8_t channel)
{
	uint32_t raw = bspAinGetRawValue(channel);
	return (raw * 3300U) / 4095U;
}

uint32_t sensorReadDivMv(uint8_t channel, uint32_t rHigh, uint32_t rLow)
{
	uint32_t vAdc = sensorReadMv(channel);
	return (vAdc * (rHigh + rLow)) / rLow;
}

/*
 * Mains AC voltage at AIN_ADC_VIN (PC2_C, ADC3_INP0 — direct channel).
 * Vadc = 1.65 + (0.4 x 1.414 x Vin x 50.2 / (750x4 + 500 + 75)) x 3.9 / 6.49
 *        denominator = 3000 + 575 = 3575
 *   => Vadc = 1.65 + Vin x (0.4 x 1.414 x 50.2 / 3575 x 3.9 / 6.49)
 *   => Vadc = 1.65 + Vin x 0.004773
 *   => Vin  = (Vadc - 1.65) / 0.004773
 * ADC output range: 0.568 V .. 2.732 V.
 * All in mV.  Returns 0 if Vadc < 1.65 V.
 */
uint32_t sensorReadVinMv(void)
{
	int32_t vAdc = (int32_t)sensorReadMv(AIN_ADC_VIN);
	int32_t delta = vAdc - 1650;
	if (delta <= 0) {
		return 0;
	}
	return (uint32_t)(delta * 20953U / 100U);
}
