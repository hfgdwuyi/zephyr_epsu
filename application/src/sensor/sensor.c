/*
 * sensor.c
 *
 * ADC-based sensor interpretation — application layer.
 *
 * Temperature (NTC): thermistor R→T conversion via lookup table + linear
 *   interpolation.  NTC: R25=10000Ω, B(25/85)≈3984K (table from NTC data.txt).
 *   Voltage divider:  Vcc ─── Rf(1000Ω) ─── ADC ─── NTC ─── GND
 *   Vadc = Vref × Rntc / (Rf + Rntc)  ⇒  Rntc = Rf × Vadc / (Vref - Vadc)
 *   where Vref=3.3V, Rf=1000Ω, Vadc = raw × 3.3 / 4095
 *
 * Voltage: external scaling per channel - the 24V rail (PDC0 PA6, PDC0_ALT
 *   PC3_C) uses an op-amp (×10); the other PDC rails use a 47k:4.7k divider (×11).
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

/* Standard library */
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
	NTC_HW_RFIXED_OHM  = 1000,    /* upper arm Rf (1k) */
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
 * 10k NTC, -55..150°C in 1°C steps (R nom from NTC data.txt, R25=10kΩ).
 * Interpolation provides sub-1°C resolution.
 */
static const ntc_point_t ntc_table[] = {
	{   -550,   953774 }, /*  -55.0 C */
	{   -540,   886261 }, /*  -54.0 C */
	{   -530,   823959 }, /*  -53.0 C */
	{   -520,   766433 }, /*  -52.0 C */
	{   -510,   713290 }, /*  -51.0 C */
	{   -500,   664169 }, /*  -50.0 C */
	{   -490,   618743 }, /*  -49.0 C */
	{   -480,   576711 }, /*  -48.0 C */
	{   -470,   537801 }, /*  -47.0 C */
	{   -460,   501761 }, /*  -46.0 C */
	{   -450,   468363 }, /*  -45.0 C */
	{   -440,   437399 }, /*  -44.0 C */
	{   -430,   408676 }, /*  -43.0 C */
	{   -420,   382019 }, /*  -42.0 C */
	{   -410,   357268 }, /*  -41.0 C */
	{   -400,   334274 }, /*  -40.0 C */
	{   -390,   312904 }, /*  -39.0 C */
	{   -380,   293034 }, /*  -38.0 C */
	{   -370,   274548 }, /*  -37.0 C */
	{   -360,   257343 }, /*  -36.0 C */
	{   -350,   241323 }, /*  -35.0 C */
	{   -340,   226399 }, /*  -34.0 C */
	{   -330,   212490 }, /*  -33.0 C */
	{   -320,   199521 }, /*  -32.0 C */
	{   -310,   187423 }, /*  -31.0 C */
	{   -300,   176133 }, /*  -30.0 C */
	{   -290,   165591 }, /*  -29.0 C */
	{   -280,   155746 }, /*  -28.0 C */
	{   -270,   146545 }, /*  -27.0 C */
	{   -260,   137944 }, /*  -26.0 C */
	{   -250,   129900 }, /*  -25.0 C */
	{   -240,   122374 }, /*  -24.0 C */
	{   -230,   115329 }, /*  -23.0 C */
	{   -220,   108732 }, /*  -22.0 C */
	{   -210,   102553 }, /*  -21.0 C */
	{   -200,    96761 }, /*  -20.0 C */
	{   -190,    91332 }, /*  -19.0 C */
	{   -180,    86239 }, /*  -18.0 C */
	{   -170,    81461 }, /*  -17.0 C */
	{   -160,    76976 }, /*  -16.0 C */
	{   -150,    72765 }, /*  -15.0 C */
	{   -140,    68809 }, /*  -14.0 C */
	{   -130,    65091 }, /*  -13.0 C */
	{   -120,    61596 }, /*  -12.0 C */
	{   -110,    58310 }, /*  -11.0 C */
	{   -100,    55218 }, /*  -10.0 C */
	{    -90,    52308 }, /*   -9.0 C */
	{    -80,    49569 }, /*   -8.0 C */
	{    -70,    46989 }, /*   -7.0 C */
	{    -60,    44559 }, /*   -6.0 C */
	{    -50,    42268 }, /*   -5.0 C */
	{    -40,    40108 }, /*   -4.0 C */
	{    -30,    38071 }, /*   -3.0 C */
	{    -20,    36150 }, /*   -2.0 C */
	{    -10,    34336 }, /*   -1.0 C */
	{      0,    32624 }, /*    0.0 C */
	{     10,    31007 }, /*    1.0 C */
	{     20,    29480 }, /*    2.0 C */
	{     30,    28036 }, /*    3.0 C */
	{     40,    26672 }, /*    4.0 C */
	{     50,    25381 }, /*    5.0 C */
	{     60,    24161 }, /*    6.0 C */
	{     70,    23006 }, /*    7.0 C */
	{     80,    21912 }, /*    8.0 C */
	{     90,    20877 }, /*    9.0 C */
	{    100,    19897 }, /*   10.0 C */
	{    110,    18968 }, /*   11.0 C */
	{    120,    18088 }, /*   12.0 C */
	{    130,    17253 }, /*   13.0 C */
	{    140,    16462 }, /*   14.0 C */
	{    150,    15711 }, /*   15.0 C */
	{    160,    14999 }, /*   16.0 C */
	{    170,    14323 }, /*   17.0 C */
	{    180,    13681 }, /*   18.0 C */
	{    190,    13072 }, /*   19.0 C */
	{    200,    12493 }, /*   20.0 C */
	{    210,    11943 }, /*   21.0 C */
	{    220,    11420 }, /*   22.0 C */
	{    230,    10923 }, /*   23.0 C */
	{    240,    10450 }, /*   24.0 C */
	{    250,    10000 }, /*   25.0 C */
	{    260,     9572 }, /*   26.0 C */
	{    270,     9165 }, /*   27.0 C */
	{    280,     8777 }, /*   28.0 C */
	{    290,     8408 }, /*   29.0 C */
	{    300,     8056 }, /*   30.0 C */
	{    310,     7721 }, /*   31.0 C */
	{    320,     7401 }, /*   32.0 C */
	{    330,     7097 }, /*   33.0 C */
	{    340,     6807 }, /*   34.0 C */
	{    350,     6530 }, /*   35.0 C */
	{    360,     6266 }, /*   36.0 C */
	{    370,     6014 }, /*   37.0 C */
	{    380,     5773 }, /*   38.0 C */
	{    390,     5543 }, /*   39.0 C */
	{    400,     5324 }, /*   40.0 C */
	{    410,     5114 }, /*   41.0 C */
	{    420,     4914 }, /*   42.0 C */
	{    430,     4723 }, /*   43.0 C */
	{    440,     4540 }, /*   44.0 C */
	{    450,     4365 }, /*   45.0 C */
	{    460,     4198 }, /*   46.0 C */
	{    470,     4038 }, /*   47.0 C */
	{    480,     3885 }, /*   48.0 C */
	{    490,     3739 }, /*   49.0 C */
	{    500,     3599 }, /*   50.0 C */
	{    510,     3465 }, /*   51.0 C */
	{    520,     3336 }, /*   52.0 C */
	{    530,     3213 }, /*   53.0 C */
	{    540,     3095 }, /*   54.0 C */
	{    550,     2982 }, /*   55.0 C */
	{    560,     2874 }, /*   56.0 C */
	{    570,     2770 }, /*   57.0 C */
	{    580,     2671 }, /*   58.0 C */
	{    590,     2575 }, /*   59.0 C */
	{    600,     2484 }, /*   60.0 C */
	{    610,     2396 }, /*   61.0 C */
	{    620,     2312 }, /*   62.0 C */
	{    630,     2231 }, /*   63.0 C */
	{    640,     2153 }, /*   64.0 C */
	{    650,     2079 }, /*   65.0 C */
	{    660,     2007 }, /*   66.0 C */
	{    670,     1938 }, /*   67.0 C */
	{    680,     1872 }, /*   68.0 C */
	{    690,     1809 }, /*   69.0 C */
	{    700,     1748 }, /*   70.0 C */
	{    710,     1689 }, /*   71.0 C */
	{    720,     1633 }, /*   72.0 C */
	{    730,     1578 }, /*   73.0 C */
	{    740,     1526 }, /*   74.0 C */
	{    750,     1476 }, /*   75.0 C */
	{    760,     1428 }, /*   76.0 C */
	{    770,     1381 }, /*   77.0 C */
	{    780,     1336 }, /*   78.0 C */
	{    790,     1293 }, /*   79.0 C */
	{    800,     1252 }, /*   80.0 C */
	{    810,     1212 }, /*   81.0 C */
	{    820,     1173 }, /*   82.0 C */
	{    830,     1136 }, /*   83.0 C */
	{    840,     1101 }, /*   84.0 C */
	{    850,     1066 }, /*   85.0 C */
	{    860,     1033 }, /*   86.0 C */
	{    870,     1001 }, /*   87.0 C */
	{    880,      970 }, /*   88.0 C */
	{    890,      940 }, /*   89.0 C */
	{    900,      912 }, /*   90.0 C */
	{    910,      884 }, /*   91.0 C */
	{    920,      857 }, /*   92.0 C */
	{    930,      831 }, /*   93.0 C */
	{    940,      806 }, /*   94.0 C */
	{    950,      782 }, /*   95.0 C */
	{    960,      759 }, /*   96.0 C */
	{    970,      737 }, /*   97.0 C */
	{    980,      715 }, /*   98.0 C */
	{    990,      694 }, /*   99.0 C */
	{   1000,      674 }, /*  100.0 C */
	{   1010,      655 }, /*  101.0 C */
	{   1020,      636 }, /*  102.0 C */
	{   1030,      618 }, /*  103.0 C */
	{   1040,      600 }, /*  104.0 C */
	{   1050,      583 }, /*  105.0 C */
	{   1060,      566 }, /*  106.0 C */
	{   1070,      550 }, /*  107.0 C */
	{   1080,      535 }, /*  108.0 C */
	{   1090,      520 }, /*  109.0 C */
	{   1100,      506 }, /*  110.0 C */
	{   1110,      492 }, /*  111.0 C */
	{   1120,      478 }, /*  112.0 C */
	{   1130,      465 }, /*  113.0 C */
	{   1140,      452 }, /*  114.0 C */
	{   1150,      440 }, /*  115.0 C */
	{   1160,      428 }, /*  116.0 C */
	{   1170,      417 }, /*  117.0 C */
	{   1180,      406 }, /*  118.0 C */
	{   1190,      395 }, /*  119.0 C */
	{   1200,      384 }, /*  120.0 C */
	{   1210,      374 }, /*  121.0 C */
	{   1220,      364 }, /*  122.0 C */
	{   1230,      355 }, /*  123.0 C */
	{   1240,      346 }, /*  124.0 C */
	{   1250,      337 }, /*  125.0 C */
	{   1260,      328 }, /*  126.0 C */
	{   1270,      320 }, /*  127.0 C */
	{   1280,      311 }, /*  128.0 C */
	{   1290,      304 }, /*  129.0 C */
	{   1300,      296 }, /*  130.0 C */
	{   1310,      288 }, /*  131.0 C */
	{   1320,      281 }, /*  132.0 C */
	{   1330,      274 }, /*  133.0 C */
	{   1340,      267 }, /*  134.0 C */
	{   1350,      261 }, /*  135.0 C */
	{   1360,      254 }, /*  136.0 C */
	{   1370,      248 }, /*  137.0 C */
	{   1380,      242 }, /*  138.0 C */
	{   1390,      236 }, /*  139.0 C */
	{   1400,      230 }, /*  140.0 C */
	{   1410,      225 }, /*  141.0 C */
	{   1420,      219 }, /*  142.0 C */
	{   1430,      214 }, /*  143.0 C */
	{   1440,      209 }, /*  144.0 C */
	{   1450,      204 }, /*  145.0 C */
	{   1460,      199 }, /*  146.0 C */
	{   1470,      195 }, /*  147.0 C */
	{   1480,      190 }, /*  148.0 C */
	{   1490,      186 }, /*  149.0 C */
	{   1500,      181 }, /*  150.0 C */
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

/* PDC1..7: resistive divider 47k:4.7k → actual rail mV.
 * Vactual = Vadc × (470 + 47) / 47 = Vadc × 11.  All in 0.1kΩ units. */
#define SENSOR_DIV_RHIGH 470
#define SENSOR_DIV_RLOW  47

static uint32_t divMvFromRaw(uint32_t raw)
{
	uint32_t vAdc = mvFromRaw(raw);
	return (vAdc * (SENSOR_DIV_RHIGH + SENSOR_DIV_RLOW)) / SENSOR_DIV_RLOW;
}

/* 24V rail: PDC0 (PA6, ADC1_INP3) and PDC0_ALT (PC3_C, ADC3_INP1) - op-amp
 * stage with gain 4.7/47 (NOT a divider).
 *   Vadc = Vrail × 4.7k/47k = Vrail / 10   →   Vrail = Vadc × 10. */
#define SENSOR_PDC24_RF   470U   /* 47k  feedback (0.1kΩ units) */
#define SENSOR_PDC24_RIN   47U   /* 4.7k input    (0.1kΩ units) */

static uint32_t pdc24vMvFromRaw(uint32_t raw)
{
	uint32_t vAdc = mvFromRaw(raw);
	return (vAdc * SENSOR_PDC24_RF) / SENSOR_PDC24_RIN;   /* ×10 */
}

/* 2:1 divider rails (3V3 / 5V0 monitors): Vactual = Vadc × 2.
 * These two rails use a 2:1 divider, so the firmware multiplies by 2. */
static uint32_t div2MvFromRaw(uint32_t raw)
{
	return mvFromRaw(raw) * 2U;
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
	{ AIN_ADC_PDC0,     1,  2, false, pdc24vMvFromRaw },   /* 24V rail: op-amp ×10 */
	{ AIN_ADC_PDC1,     1,  2, false, divMvFromRaw },
	{ AIN_ADC_PDC2,     1,  2, false, divMvFromRaw },
	{ AIN_ADC_PDC3,     1,  2, false, divMvFromRaw },
	{ AIN_ADC_PDC4,     1,  2, false, divMvFromRaw },
	{ AIN_ADC_PDC5,     1,  2, false, divMvFromRaw },
	{ AIN_ADC_PDC6,     1,  2, false, divMvFromRaw },
	{ AIN_ADC_PDC7,     1,  2, false, divMvFromRaw },
	{ AIN_ADC_PDC0_ALT, 1,  2, false, pdc24vMvFromRaw },   /* 24V rail: op-amp ×10 */
	/* monitor rails: 500 ms */
	{ AIN_ADC_12V,     10,  2, false, mvFromRaw },     /* direct: value as measured */
	{ AIN_ADC_5V0,     10,  2, false, div2MvFromRaw }, /* 2:1 divider */
	{ AIN_ADC_3V3,     10,  2, false, div2MvFromRaw },
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
