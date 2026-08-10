/*
 * ac_meter.h — AC mains voltage / frequency meter — cios-zhong
 *
 * Measures the AC input voltage (mains, ~230 V / 50 Hz) using two signals:
 *
 *   zero_en  — PC14, external TL3310 comparator zero-crossing output.
 *              A toggling square wave at 2x mains frequency (50 Hz AC
 *              produces a 100 Hz square wave). A GPIO EXTI edge callback
 *              stamps k_cycle_get_32() timestamps and derives the mains
 *              period → frequency.
 *
 *   adc_vin  — AIN_ADC_VIN (PC2_C, ADC3_INP0). Sampled once per
 *              acMeterUpdate() tick (~1 kHz) by the ac_meter thread. The
 *              samples fill a ring buffer one mains period long; a sliding
 *              window RMS (DC-offset self-calibrated from the window mean)
 *              yields the true RMS mains voltage.
 *
 * The zero_en edge timestamp is the gating signal: it tells the RMS engine
 * how long one period is, so the integration window spans exactly one cycle
 * and stays correct across 47–63 Hz. The RMS window phase is irrelevant
 * because a whole-period RMS is independent of integration start phase.
 *
 * The AC meter module owns AIN_ADC_VIN exclusively — bspAinPoll() skips it.
 * Results are display/query only: the state machine does not use them.
 *
 * Thread model: a dedicated k_thread in the scheduler calls acMeterUpdate()
 * every AC_METER_BASE_PERIOD_US. The zero_en callback runs in ISR context and
 * only writes timestamps + an edge counter (volatile), never blocking.
 *
 * Depends on: bsp_ain.h (AIN_ADC_VIN), zero-en-gpios in app.overlay.
 */

#ifndef AC_METER_H
#define AC_METER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AC meter thread cadence — the scheduler calls acMeterUpdate() every tick.
 * 1 kHz → 20 samples per 50 Hz period (RMS error ~1–2% on real mains).
 * Raise to 500 us for 2 kHz without any other change. */
#define AC_METER_BASE_PERIOD_US  1000

/*
 * One processing tick (~1 kHz): sample adc_vin, slide the RMS window, and
 * refresh the published Vin_rms / freq / ac-present values. Also expires
 * the zero-crossing signal (ac_present=false) if no edge arrived for a while.
 * Called by the scheduler's ac_meter thread.
 */
void acMeterUpdate(void);

/* Zero-crossing / frequency handling — called from the GPIO EXTI callback. */
void acMeterZeroEdge(void);

/* ---- Published results (cross-thread, volatile) ---- */

/* True RMS mains voltage in mV (0 = not measured / no AC). */
uint32_t acMeterGetVinRmsMv(void);

/* Mains frequency × 10 (e.g., 500 = 50.0 Hz); 0 = unknown. */
int16_t acMeterGetVinFreq(void);

/* True while a valid zero-crossing signal is present. */
bool acMeterAcPresent(void);

/* Reset internal state (window, counters). */
void acMeterInit(void);

#ifdef __cplusplus
}
#endif

#endif /* AC_METER_H */
