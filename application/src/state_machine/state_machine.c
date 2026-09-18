/*!
 * @file state_machine.c
 * @brief Top-level state machine - main-mode detection, dispatch, transition log
 *
 * Design:
 *   1. Main mode is decided once after power-on: a valid DIP pattern must hold
 *      SYSTEM_CONFIRM_MS (1s) to latch; a change or conflict aborts it, and once
 *      locked it is never re-evaluated (no runtime switching).
 *   2. Invalid/conflicting DIP: enter no system and drive no output (safe state).
 *   2b. Trolley debounce: a valid level must hold TROLLEY_DEBOUNCE_MS (2s) to be
 *       treated as connected; an invalid level disconnects immediately.
 *   3. S1 system: fully delegated to sm_s1.c (T0~T4/RESET).
 *   4. S2 system: fully delegated to sm_s2.c (T0~T4/RESET).
 *   5. Only a "tick" API is exported: no query API, no host commands -
 *      state info is printed to the console on transitions.
 *      （STATEMACHINE: system -> … / S1: -> … / S2: -> …）
 *
 */
/*----------------------------------------------------------------------------*/

/* Zephyr */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

/* BSP */
#include "bsp_ain.h"    
#include "bsp_dio.h"    /* DOUT_LED_GRID_PWR_IN / DOUT_LED_UPS_IN */

/* Application */
#include "sensor.h"     /* sensorGetPhys() */
#include "state_machine.h"
#include "sm_s1.h"
#include "sm_s2.h"
#include "terminal.h"   /* terminalIsQuiet(): silence logs during DFU */

/* ==================== Main mode (module-private) ==================== */

typedef enum {
	SYSTEM_NONE = 0,   /* invalid DIP: enter no system       */
	SYSTEM_S1,         /* S1 system                          */
	SYSTEM_S2,         /* S2 system (solo / classic)         */
} smSystem_t;

static smSystem_t g_system = SYSTEM_NONE;
static bool       g_system_locked;   /* true = main mode latched, no re-eval */

/* Pending main mode: a valid DIP pattern is timed for SYSTEM_CONFIRM_MS */
static smSystem_t g_pending = SYSTEM_NONE;
static bool       g_pending_timing;
static int64_t    g_pending_ms;

/* Trolley debounce state */
static bool    g_trolley_conn;      /* debounced connected state */
static bool    g_trolley_timing;
static int64_t g_trolley_since;

/* Per tick: hold TROLLEY_DEBOUNCE_MS to connect; an invalid level disconnects now */
static void trolleyDebounce(uint32_t din)
{
	const bool raw = isTrolleyConnected(din);

	if (!raw) {
		g_trolley_conn   = false;
		g_trolley_timing = false;
		return;
	}
	if (g_trolley_conn) {
		return;   /* already connected, keep */
	}
	if (!g_trolley_timing) {
		g_trolley_timing = true;
		g_trolley_since  = k_uptime_get();
	} else if ((k_uptime_get() - g_trolley_since) >= TROLLEY_DEBOUNCE_MS) {
		g_trolley_conn = true;   /* valid for 2 s -> connected */
	}
}

bool isTrolleyConnectedDebounced(void)
{
	return g_trolley_conn;
}

static const char *systemName(smSystem_t sys)
{
	switch (sys) {
	case SYSTEM_S1: return "S1";
	case SYSTEM_S2: return "S2";
	default:        return "none (invalid dip)";
	}
}

/* ==================== Lifecycle ==================== */

void stateMachineInit(void)
{
	g_system = SYSTEM_NONE;
	g_system_locked = false;

	printk("STATEMACHINE: init\n");
}

void stateMachineTick(void)
{
	const uint32_t din = bspDinGetBitmap();   /* one input snapshot per tick */

	/* Main-mode decision:
	 *   - not locked: hold SYSTEM_CONFIRM_MS to enter and latch;
	 *   - locked: only both config signals low releases the lock (re-evaluate);
	 *     if either is high, keep the current mode and do not switch. */
	const bool cfg_s1   = (din & BIT(DIN_S1_SYSTEM_CONFIG))   != 0U;
	const bool cfg_s2   = (din & BIT(DIN_S2_SYSTEM_CONFIG))   != 0U;
	const bool cfg_solo = (din & BIT(DIN_SOLO_SYSTEM_CONFIG)) != 0U;

	if (g_system_locked) {
		if (!cfg_s1 && !cfg_s2) {
			g_system         = SYSTEM_NONE;   /* DIPs released -> unlock */
			g_system_locked  = false;
			g_pending        = SYSTEM_NONE;
			g_pending_timing = false;
			if (!terminalIsQuiet()) {
				printk("STATEMACHINE: dip released -> re-detect\n");
			}
		}
		/* otherwise keep the current mode (no switch) */
	} else {
		const bool s1 = cfg_s1;
		const bool s2 = cfg_s2;

		/* Decision (must hold SYSTEM_CONFIRM_MS to take effect):
		 *   s1 && !s2 && !solo -> S1
		 *   !s1 &&  s2         -> S2 (solo selects solo/classic inside S2)
		 *   anything else (both active / both inactive / s1 with solo) -> invalid
		 * NOTE: solo additionally vetoes S1 (avoids S1/solo coupling); if solo turns
		 *       active inside the 1 s window, 'want' becomes NONE and the timer restarts. */
		smSystem_t want;

		if (s1 && !s2 && !cfg_solo) {
			want = SYSTEM_S1;
		} else if (!s1 && s2) {
			want = SYSTEM_S2;
		} else {
			want = SYSTEM_NONE;
		}

		if (want == SYSTEM_NONE) {
			g_pending        = SYSTEM_NONE;
			g_pending_timing = false;    /* invalid/conflict -> cancel, stay out */
		} else if (want != g_pending || !g_pending_timing) {
			g_pending        = want;     /* new valid mode: restart the 1 s timer */
			g_pending_timing = true;
			g_pending_ms     = k_uptime_get();
		} else if ((k_uptime_get() - g_pending_ms) >= SYSTEM_CONFIRM_MS) {
			g_system        = want;      /* held 1 s -> latch and enter */
			g_system_locked = true;

			/* Print the mode before entering the sub-state so the log order is
			 * "STATEMACHINE: system -> S1" then "S1: -> STANDBY ..." */
			if (!terminalIsQuiet()) {
				printk("STATEMACHINE: system -> %s\n", systemName(want));
			}

			switch (want) {
			case SYSTEM_S1: smS1Enter(); break;
			case SYSTEM_S2: smS2Enter(); break;
			default:        break;
			}
		}
	}

	/* Trolley debounce (once per tick; sm_s1/sm_s2 read it via the API) */
	trolleyDebounce(din);

	switch (g_system) {
	case SYSTEM_S1:
		smS1Tick(din);
		break;

	case SYSTEM_S2:
		smS2Tick(din);
		break;

	case SYSTEM_NONE:
		/* No system (invalid/conflict) or unlocked: drive nothing (safe state) */
		doutWrite(SM_RELAY_ALL, 0);
		doutWrite(SM_LED_ALL, 0);
		doutWrite(SM_DRV_ALL, 0);
		break;
	}

	/* ---- Grid/UPS indicators (PD2/PD3, global) ----
	 * Condition: ME_BOX_ERROR high AND PA6 (AIN_ADC_PDC0) > 2.2 V.
	 * True -> mains LED (PD2); false -> UPS LED (PD3). */
	const bool mains_ok = isMainsOk(din) &&
			      (sensorGetPhys(AIN_ADC_PDC0) > 2200U);

	bspDoutSetBitmap(BIT64(DOUT_LED_GRID_PWR_IN), mains_ok);   /* PD2 mains */
	bspDoutSetBitmap(BIT64(DOUT_LED_UPS_IN), !mains_ok);       /* PD3 UPS  */

	/* ---- IS_PC / APP_HOST indicators (PD7/PD12, global) ----
 * Follow the inputs: IS_PC_ON active -> PD7; APP_HOST_ON active -> PD12. */
	bspDoutSetBitmap(LED_IS_PC_ON, isPcOn(din));               /* PD7  */
	bspDoutSetBitmap(LED_APP_HOST_ON, isAppHostOn(din));       /* PD12 */
}
