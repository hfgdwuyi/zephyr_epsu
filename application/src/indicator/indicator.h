/*!
 * @file indicator.h
 * @brief Status indicator (DAC1_OUT2 / PA5) - application layer
 *
 * The state machine selects a mode via indicatorSetMode():
 *   INDICATOR_BREATH : standby - 0 -> 1.5 V -> 0 triangle, 4000 ms period
 *   INDICATOR_ON     : running - constant 1.5 V
 *   INDICATOR_OFF    : shutdown / reset - 0 V
 *   INDICATOR_MANUAL : driven directly by bspAoutWrite() (debug)
 *
 * While the on/off key is held (indicatorSetBreathFast(true)) the same breathing
 * waveform is used with half the period (double frequency) as key feedback.
 *
 * indicatorUpdate() is called periodically by the scheduler. The DAC primitives
 * stay in the BSP (bsp_aout.c).
 */
/*----------------------------------------------------------------------------*/
#ifndef INDICATOR_H
#define INDICATOR_H

/* Standard library */
#include <stdbool.h>
#include <stdint.h>

/* Indicator modes */
typedef enum {
	INDICATOR_OFF = 0,   /* off            */
	INDICATOR_BREATH,    /* 0->1.5V->0 breathing (4 s) */
	INDICATOR_ON,        /* constant 1.5 V */
	INDICATOR_MANUAL,    /* driven by bspAoutWrite() directly */
} indicatorMode_t;

/*! Set the indicator mode (applies immediately) */
void indicatorSetMode(indicatorMode_t mode);

/*! Current indicator mode */
indicatorMode_t indicatorGetMode(void);

/*! Double the breathing frequency (true while the on/off key is held) */
void indicatorSetBreathFast(bool fast);

/*! Periodic refresh (called by the scheduler) */
void indicatorUpdate(void);

#endif /* INDICATOR_H */
