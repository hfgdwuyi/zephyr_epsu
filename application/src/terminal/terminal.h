/*
 * terminal.h — change-triggered sensor value printing — cios-zhong
 *
 * The scheduler owns a terminal thread that calls terminalUpdate() every base
 * period (scheduler.c). Each tick compares the current sensor physical values
 * against the last printed ones and prints only when a value has moved
 * meaningfully (temperature > 3 °C, voltage > 3 V), plus one initial snapshot
 * at startup. This keeps the log quiet unless something actually changes.
 *
 * Depends on: sensor.h (published physical values), bsp_ain.h (channel names)
 */

#ifndef TERMINAL_H
#define TERMINAL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Terminal thread cadence — the scheduler calls terminalUpdate() every base tick. */
#define TERMINAL_BASE_PERIOD_MS  500

/* Print only when a physical value has moved meaningfully */
#define TERMINAL_TEMP_DELTA  30     /* 3.0 °C, in ×10 units */
#define TERMINAL_MV_DELTA   3000    /* 3.0 V, in mV */
#define TERMINAL_FREQ_DELTA   5     /* 0.5 Hz, in ×10 units (AC mains) */

/* Process one terminal tick: print sensor values that changed meaningfully. */
void terminalUpdate(void);

#ifdef __cplusplus
}
#endif

#endif /* TERMINAL_H */
