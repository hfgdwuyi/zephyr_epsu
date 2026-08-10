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
 * Voltage: external-circuit scaling for the PDC dividers (47k:4.7k).
 *   Mains AC voltage is measured by the ac_meter module (ac_meter.c), which
 *   owns AIN_ADC_VIN.  bsp_ain only exposes raw ADC counts.
 *
 * Thread model:
 *   The scheduler owns a dedicated sensor thread and calls sensorUpdate()
 *   each base period (see SENSOR_BASE_PERIOD_MS in sensor.h). Each tick reads
 *   the raw ADC snapshot (filled by bspAinPoll), low-pass filters every
 *   configured channel, and every update_n ticks converts + publishes the
 *   physical value into phys_cache. This gives per-quantity sample rates from
 *   a single base tick (multi-rate decimation). Over-temp duration is
 *   accumulated here too, so consumers only query.
 */

/* C standard library */
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

/* Zephyr */
#include <zephyr/sys/util.h>

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

/*
 * NTC raw ADC count → temperature × 10.
 * Returns INT16_MIN on fault (ADC not ready, open, or short to Vcc).
 */
static int16_t tempFromRaw(uint32_t raw)
{
	if (raw == 0 || raw >= NTC_HW_ADC_MAX) {
		return INT16_MIN;   /* open / short to Vcc  */
	}

	/* Vadc(mV) = raw × Vref / ADCmax */
	uint32_t v_mv = (raw * NTC_HW_VREF_MV) / NTC_HW_ADC_MAX;

	if (v_mv >= NTC_HW_VREF_MV) {
		return INT16_MIN;   /* NTC disconnected or short to Vref */
	}

	uint32_t v_drop_mv = NTC_HW_VREF_MV - v_mv;
	uint32_t r_ntc = (v_drop_mv == 0)
		? 0U
		: (uint32_t)((uint64_t)NTC_HW_RFIXED_OHM * v_mv / v_drop_mv);

	return ntcInterpolate(r_ntc);
}

int16_t sensorReadTemp(uint8_t ain_channel)
{
	return tempFromRaw(bspAinGetRawValue(ain_channel));
}

/* ---- Temperature monitoring service ----
 * Updated by the sensor thread; read cross-thread by the state machine. */

static volatile int16_t  s_temp1;       /* temp sensor 1 (×10°C)     */
static volatile int16_t  s_temp2;       /* temp sensor 2 (×10°C)     */
static volatile uint32_t s_overtemp_ms; /* consecutive over-temp ms  */

void sensorTempInit(void)
{
	s_temp1 = 0;
	s_temp2 = 0;
	s_overtemp_ms = 0;
}

int16_t sensorTempGet1(void)
{
	return s_temp1;
}

int16_t sensorTempGet2(void)
{
	return s_temp2;
}

int16_t sensorTempGetMax(void)
{
	return (s_temp1 > s_temp2) ? s_temp1 : s_temp2;
}

/* True if either NTC sensor read failed (open/short). tempFromRaw returns
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

/* ==================== Voltage conversions ==================== */

/* raw ADC count → mV at the pin (0–3300 mV) */
static uint32_t mvFromRaw(uint32_t raw)
{
	return (raw * 3300U) / 4095U;
}

/* PDC divider 47k:4.7k → actual rail mV.
 * Vactual = Vadc × (470 + 47) / 47 = Vadc × 11.  All in 0.1kΩ units. */
#define SENSOR_DIV_RHIGH 470
#define SENSOR_DIV_RLOW  47

static uint32_t divMvFromRaw(uint32_t raw)
{
	uint32_t vAdc = mvFromRaw(raw);
	return (vAdc * (SENSOR_DIV_RHIGH + SENSOR_DIV_RLOW)) / SENSOR_DIV_RLOW;
}

/* NOTE: mains AC voltage (AIN_ADC_VIN) is no longer converted here — the
 * ac_meter module owns that channel and publishes a true windowed RMS value.
 * AIN_ADC_VIN is excluded from bspAinPoll(); see ac_meter.c. */

/* ==================== Multi-rate sampling thread ==================== */

typedef uint32_t (*sensorConvertFn_t)(uint32_t filtered_raw);

/* Per-channel processing config. update_n gives multi-rate decimation:
 * a channel publishes its physical value every update_n base ticks. */
typedef struct {
	uint8_t           chan;           /* AIN channel (bsp_ain.h index) */
	uint8_t           update_n;       /* publish every N base ticks    */
	uint8_t           filter_shift;   /* IIR: filtered += (raw-filtered)>>shift; 0 = none */
	bool              is_temp;        /* true → also publish to s_temp1/2 */
	sensorConvertFn_t convert;
} sensorChanCfg_t;

/* tempFromRaw returns int16_t; wrap so the generic converter is uniform. */
static uint32_t tempFromRawU(uint32_t raw)
{
	return (uint32_t)tempFromRaw(raw);
}

static const sensorChanCfg_t sensor_cfg[] = {
	/* PDC rails: base rate (50 ms) — power output, fastest quantity */
	{ AIN_ADC_PDC0,     1,  2, false, divMvFromRaw },
	{ AIN_ADC_PDC1,     1,  2, false, divMvFromRaw },
	{ AIN_ADC_PDC2,     1,  2, false, divMvFromRaw },
	{ AIN_ADC_PDC3,     1,  2, false, divMvFromRaw },
	{ AIN_ADC_PDC4,     1,  2, false, divMvFromRaw },
	{ AIN_ADC_PDC5,     1,  2, false, divMvFromRaw },
	{ AIN_ADC_PDC6,     1,  2, false, divMvFromRaw },
	{ AIN_ADC_PDC7,     1,  2, false, divMvFromRaw },
	{ AIN_ADC_PDC0_ALT, 1,  2, false, divMvFromRaw },
	/* monitor rails: 500 ms */
	{ AIN_ADC_12V,     10,  2, false, mvFromRaw },
	{ AIN_ADC_5V0,     10,  2, false, mvFromRaw },
	{ AIN_ADC_3V3,     10,  2, false, mvFromRaw },
	/* NTC temperature: 1 s — slow thermal time constant */
	{ AIN_ADC_TEMP1,   20,  3, true,  tempFromRawU },
	{ AIN_ADC_TEMP2,   20,  3, true,  tempFromRawU },
};

static volatile uint32_t filtered_raw[BSP_AIN_NUMBER];   /* IIR state per channel */
static volatile uint32_t phys_cache[BSP_AIN_NUMBER];     /* published values      */
static uint8_t           filt_init[BSP_AIN_NUMBER];      /* 1 once first sample seen */
static uint8_t           update_ctr[BSP_AIN_NUMBER];     /* decimation countdown   */

void sensorUpdate(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(sensor_cfg); i++) {
		const sensorChanCfg_t *cfg = &sensor_cfg[i];
		uint8_t ch = cfg->chan;
		uint32_t raw = bspAinGetRawValue(ch);

		/* first-order IIR low-pass on raw counts; snap on first sample */
		if (!filt_init[ch]) {
			filtered_raw[ch] = raw;
			filt_init[ch] = 1;
		} else if (cfg->filter_shift != 0) {
			int32_t diff = (int32_t)raw - (int32_t)filtered_raw[ch];
			filtered_raw[ch] += (uint32_t)(diff >> cfg->filter_shift);
		} else {
			filtered_raw[ch] = raw;
		}

		/* multi-rate decimation: publish every update_n base ticks */
		if (++update_ctr[ch] < cfg->update_n) {
			continue;
		}
		update_ctr[ch] = 0;

		uint32_t phys = cfg->convert(filtered_raw[ch]);
		if (cfg->is_temp) {
			int16_t t = (int16_t)phys;
			if (ch == AIN_ADC_TEMP1) {
				s_temp1 = t;
			} else if (ch == AIN_ADC_TEMP2) {
				s_temp2 = t;
			}
		}
		phys_cache[ch] = phys;
	}

	/* over-temp accumulator: reset while cool, else count base ticks */
	s_overtemp_ms = sensorTempIsOvertemp()
		? s_overtemp_ms + SENSOR_BASE_PERIOD_MS
		: 0U;
}

uint32_t sensorGetPhys(uint8_t channel)
{
	if (channel >= BSP_AIN_NUMBER) {
		return 0;
	}
	return phys_cache[channel];
}
