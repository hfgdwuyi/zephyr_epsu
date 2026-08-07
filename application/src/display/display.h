/*
 * display.h — change-triggered sensor value printing — cios-zhong
 *
 * The scheduler owns a display thread that calls displayUpdate() every base
 * period (scheduler.c). Each tick compares the current sensor physical values
 * against the last printed ones and prints only when a value has moved
 * meaningfully (temperature > 3 °C, voltage > 3 V), plus one initial snapshot
 * at startup. This keeps the log quiet unless something actually changes.
 *
 * Depends on: sensor.h (published physical values), bsp_ain.h (channel names)
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Display thread cadence — the scheduler calls displayUpdate() every base tick. */
#define DISPLAY_BASE_PERIOD_MS  500

/* Print only when a physical value has moved meaningfully */
#define DISPLAY_TEMP_DELTA  30     /* 3.0 °C, in ×10 units */
#define DISPLAY_MV_DELTA   3000    /* 3.0 V, in mV */

/* Process one display tick: print sensor values that changed meaningfully. */
void displayUpdate(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_H */
