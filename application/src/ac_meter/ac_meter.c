/*
 * ac_meter.c — AC mains voltage / frequency meter — application layer.
 *
 * Two input signals (see ac_meter.h):
 *   - zero_en (PC14, TL3310 output)   → period / frequency, via EXTI edge
 *                                       callback + k_cycle_get_32().
 *   - adc_vin (AIN_ADC_VIN)           → windowed true RMS, 1 kHz thread.
 *
 * RMS math (sliding window of one mains period):
 *   Each tick appends a sample to a ring buffer and maintains running sums.
 *   Once the buffer holds N = period_us/1000 samples (one full period):
 *     mean     = sum / N
 *     variance = sumsq / N - mean^2          (int64; max ~2.4e6, no overflow)
 *     rms_ac   = isqrt(variance)             (AC component = RMS of deltas)
 *     vin_rms  = rms_ac * 20953 / 100        (Vadc = 1.65 + Vin*0.004773)
 *
 *   Because the window is exactly one period, the result is independent of
 *   the integration start phase, and the DC offset (nominal 1.65 V) is
 *   self-calibrated from the window mean rather than hard-coded.
 *
 * Validity:
 *   - Edges closer than 2 ms are rejected (TL3310 output jitter).
 *   - Periods outside [13, 33] ms (~30–75 Hz) are ignored.
 *   - If no edge arrives for > 2x longest accepted period (~66 ms), AC is
 *     declared absent and vin_rms / freq reset to 0.
 */

/* C standard library */
#include <stdbool.h>
#include <stdint.h>

/* Zephyr */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/sys/printk.h>

/* BSP */
#include "bsp_ain.h"

/* Application */
#include "ac_meter.h"

/* ==================== zero_en (TL3310) GPIO ==================== */

/* zero-en-gpios defined under the zephyr,user node in app.overlay.
 * GPIO_DT_SPEC_GET yields a brace initializer, so it must initialize a real
 * variable (can't be &-ed inline). */
static const struct gpio_dt_spec zero_en_spec =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), zero_en_gpios);

/* ==================== Constants ==================== */

/* Voltage scale: Vadc = 1.65 + Vin × 0.004773  ⇒  Vin = (Vadc − 1.65) / 0.004773.
 * Applied to the RMS of the AC deviation around the mean:
 *   delta_peak(max swing) = 230 × 1.414 × 0.004773 ≈ 1.552 V
 *   rms_delta = 1.552 / 1.414 ≈ 1.097 V  →  × 20953 / 100 ≈ 229.8 V ✓ */
#define AC_METER_VIN_SCALE_MUL 20953U
#define AC_METER_VIN_SCALE_DIV 100U

/* mV per LSB of the 12-bit ADC, VDDA = 3.3 V */
#define AC_METER_ADC_VREF_MV   3300U
#define AC_METER_ADC_MAX       4095U

/* Accepted zero-crossing frequency band (dHz, i.e. ×10): 30.0–75.0 Hz */
#define AC_METER_FREQ_MIN_DHZ  300
#define AC_METER_FREQ_MAX_DHZ  750

/* Minimum edge-to-edge gap to accept (us) — TL3310 output jitter guard */
#define AC_METER_EDGE_MIN_US   2000

/* Ring buffer default window: one 50 Hz period at 1 kHz sampling */
#define AC_METER_DEFAULT_N     20

/* RMS window length bounds (samples at 1 kHz) — 75 Hz .. 30 Hz */
#define AC_METER_N_MIN         13
#define AC_METER_N_MAX         33

/* ==================== ISR-shared state (volatile) ==================== */

/* Written by the zero_en EXTI callback (ISR); read by acMeterUpdate(). */
static volatile uint32_t s_last_edge_cycles;  /* last edge timestamp         */
static volatile uint32_t s_prev_edge_cycles;  /* same-slope edge before that */
static volatile uint32_t s_last_period_us;    /* measured mains period (us)  */
static volatile uint32_t s_edge_count;        /* total edges seen            */

static struct gpio_callback zero_en_cb;       /* zero_en EXTI callback obj   */

/* ==================== RMS window state ==================== */

static int16_t  s_buf[AC_METER_N_MAX];        /* ring buffer of samples (mV) */
static uint8_t  s_head;                       /* next write index           */
static uint8_t  s_fill;                       /* samples buffered so far    */
static uint8_t  s_n;                          /* window length (N samples)  */
static int32_t  s_sum;                        /* sum of buffered samples    */
static uint64_t s_sumsq;                      /* sum of squares             */

/* ==================== Published results (volatile) ==================== */

static volatile uint32_t s_vin_rms_mv;
static volatile int16_t  s_freq_dhz;          /* ×10, 500 = 50.0 Hz */
static volatile bool     s_ac_present;

/* ==================== Integer sqrt (bit-by-bit, no float) ==================== */

static uint32_t isqrt32(uint64_t n)
{
	uint32_t x = 0U;
	uint32_t bit = 1UL << 30;   /* highest power of 4 <= UINT32_MAX */

	while (bit > n) {
		bit >>= 2;
	}
	while (bit != 0U) {
		if (n >= ((uint64_t)x + bit)) {
			n -= (uint64_t)x + bit;
			x = (x >> 1) + bit;
		} else {
			x >>= 1;
		}
		bit >>= 2;
	}
	return x;
}

/* ==================== zero_en EXTI callback (ISR) ==================== */

/*
 * GPIO callback for zero_en. Called on every rising+falling edge (i.e., every
 * zero crossing). The mains period is the time between two consecutive
 * same-slope edges (one full cycle).
 */
static void zeroEnCallback(const struct device *port, struct gpio_callback *cb,
			   gpio_port_pins_t pins)
{
	uint32_t now = k_cycle_get_32();
	uint32_t last = s_last_edge_cycles;

	/* Reject edges too close together (TL3310 output jitter) */
	uint32_t gap_us = (uint32_t)k_cyc_to_us_floor32(now - last);
	if (gap_us < AC_METER_EDGE_MIN_US) {
		return;   /* keep last_edge so the next valid gap stays correct */
	}

	/* Same-slope edge pair → one full period */
	uint32_t period_us = (uint32_t)k_cyc_to_us_floor32(now - s_prev_edge_cycles);

	s_prev_edge_cycles = last;
	s_last_edge_cycles = now;
	s_last_period_us   = period_us;
	s_edge_count++;
}

/* ==================== Windowed RMS engine ==================== */

/* Map a zero-en-derived period (us) to a window length N. Returns 0 when the
 * period is out of the accepted range (no AC). */
static uint8_t nFromPeriod(uint32_t period_us)
{
	uint32_t n = period_us / AC_METER_BASE_PERIOD_US;

	if (n < AC_METER_N_MIN || n > AC_METER_N_MAX) {
		return 0U;
	}
	return (uint8_t)n;
}

static void resetWindow(uint8_t n)
{
	s_head  = 0;
	s_fill  = 0;
	s_sum   = 0;
	s_sumsq = 0;
	s_n     = n;
}

/*
 * Append one sample (mV) to the ring buffer; once full, return the RMS of the
 * AC deviation around the window mean (mV). Returns 0 while not yet full.
 * Sliding window removes the stale sample before inserting the new one.
 */
static uint32_t windowPush(int16_t v)
{
	if (s_fill == s_n) {
		int16_t out = s_buf[s_head];
		s_sum   -= out;
		s_sumsq -= (uint64_t)((uint32_t)(int32_t)out * (uint32_t)(int32_t)out);
	} else {
		s_fill++;
	}

	s_buf[s_head] = v;
	s_head = (uint8_t)((s_head + 1U) % s_n);

	s_sum   += v;
	s_sumsq += (uint64_t)((uint32_t)(int32_t)v * (uint32_t)(int32_t)v);

	if (s_fill < s_n) {
		return 0U;   /* window not yet full */
	}

	int32_t  mean = s_sum / (int32_t)s_n;
	uint64_t mean2 = (uint64_t)((uint32_t)(int32_t)mean * (uint32_t)(int32_t)mean);
	uint64_t var = s_sumsq / s_n - mean2;

	return isqrt32(var);
}

/* ==================== public API ==================== */

void acMeterInit(void)
{
	s_last_edge_cycles = 0;
	s_prev_edge_cycles = 0;
	s_last_period_us   = 0;
	s_edge_count       = 0;
	s_vin_rms_mv       = 0;
	s_freq_dhz         = 0;
	s_ac_present       = false;
	resetWindow(AC_METER_DEFAULT_N);

	/* zero_en GPIO: input + both-edge interrupt */
	if (!device_is_ready(zero_en_spec.port)) {
		printk("AC_METER: zero_en GPIO not ready\n");
		return;
	}
	gpio_pin_configure_dt(&zero_en_spec, GPIO_INPUT);
	gpio_init_callback(&zero_en_cb, zeroEnCallback, BIT(zero_en_spec.pin));
	gpio_add_callback(zero_en_spec.port, &zero_en_cb);
	gpio_pin_interrupt_configure_dt(&zero_en_spec, GPIO_INT_EDGE_BOTH);

	printk("AC_METER: zero_en on %s pin %u, init done\n",
	       zero_en_spec.port->name, zero_en_spec.pin);
}

void acMeterUpdate(void)
{
	/* 1) count ticks since the last zero_en edge */
	static uint32_t last_edges;
	static uint32_t ticks_since_edge;

	uint32_t edges = s_edge_count;
	if (edges != last_edges) {
		last_edges = edges;
		ticks_since_edge = 0;
	} else {
		ticks_since_edge++;
	}

	/* 2) sample adc_vin and slide the RMS window (regardless of validity) */
	int16_t v_mv = (int16_t)((bspAinReadRaw(AIN_ADC_VIN) * AC_METER_ADC_VREF_MV)
				  / AC_METER_ADC_MAX);
	uint32_t rms_ac = windowPush(v_mv);

	/* 3) zero-crossing validity: accepted period + edges still arriving */
	uint32_t period_us = s_last_period_us;
	uint8_t  n         = nFromPeriod(period_us);
	bool     valid     = (n != 0U) && (ticks_since_edge <= 2U * AC_METER_N_MAX);

	if (!valid) {
		s_ac_present = false;
		s_freq_dhz   = 0;
		s_vin_rms_mv = 0U;
		return;
	}

	/* 4) re-anchor the window length when the period changes so it always
	 * spans exactly one cycle. A reset refills in ~N ms (rms_ac stays 0). */
	if (n != s_n) {
		resetWindow(n);
		s_vin_rms_mv = 0U;
		return;
	}

	/* 5) publish frequency (×10 Hz) and RMS */
	s_freq_dhz = (int16_t)(10000000U / period_us);   /* 20,000 us → 500 */

	if (rms_ac != 0U) {
		s_vin_rms_mv = (rms_ac * AC_METER_VIN_SCALE_MUL) / AC_METER_VIN_SCALE_DIV;
	}

	s_ac_present = true;
}

uint32_t acMeterGetVinRmsMv(void)
{
	return s_vin_rms_mv;
}

int16_t acMeterGetVinFreq(void)
{
	return s_freq_dhz;
}

bool acMeterAcPresent(void)
{
	return s_ac_present;
}
