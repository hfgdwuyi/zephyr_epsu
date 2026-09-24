/*!
 * @file state_machine.c
 * @brief Top-level state machine - main-mode detection, dispatch, transition log
 *
 * Design:
 *   1. Main mode (+ the S2 solo sub-mode) is decided at power-on: the DIP
 *      pattern must hold SYSTEM_CONFIRM_MS (1s) to latch; a conflict latches the
 *      invalid error state. Once latched it is never re-evaluated while the unit
 *      is powered on (no runtime switching) - it is re-sampled only when the
 *      unit is shut down (enters T4), on SYSTEM_RESET, or after a power cycle.
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
#include "bsp_pwm.h"

/* Application */
#include "sensor.h"     /* sensorGetPhys() */
#include "indicator.h"  /* config-error / on-off-key indicator feedback */
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

/* Pending main mode: a candidate DIP pattern is timed for SYSTEM_CONFIRM_MS */
static smSystem_t g_pending = SYSTEM_NONE;
static bool       g_pending_timing;
static int64_t    g_pending_ms;

/* Latched solo sub-mode (pj4): sampled with the main mode, never live-updated */
static bool g_solo;

/* SYSTEM_RESET edge detection: a reset re-arms the main-mode decision */
static bool g_prev_reset;

/* Previous sub-state: entering T4 (shutdown) re-arms the main-mode decision */
static int g_prev_state = -1;

/* Config-error SW reset: a >= 5 s on/off key hold (on release) re-arms the DIP
 * decision, matching the S1/S2 software-reset semantics. */
#define ERR_ONOFF_RESET_MS 5000

static bool    g_err_onoff_pressed;
static int64_t g_err_onoff_start_ms;
static bool    g_err_onoff_reset_armed;

static void configErrorOnOffTick(uint32_t din)
{
	if (!g_system_locked) {
		g_err_onoff_pressed     = false;
		g_err_onoff_reset_armed = false;
		return;
	}

	const bool    onoff = isOnOffActive(din);
	const int64_t now   = k_uptime_get();

	if (onoff && !g_err_onoff_pressed) {          /* low->high: pressed */
		g_err_onoff_pressed     = true;
		g_err_onoff_start_ms    = now;
		g_err_onoff_reset_armed = false;
	} else if (onoff && g_err_onoff_pressed) {
		if ((now - g_err_onoff_start_ms) >= ERR_ONOFF_RESET_MS) {
			g_err_onoff_reset_armed = true;   /* held 5 s: reset armed */
		}
	} else if (!onoff && g_err_onoff_pressed) {   /* high->low: released */
		g_err_onoff_pressed = false;
		if (g_err_onoff_reset_armed) {
			g_err_onoff_reset_armed = false;

			/* Software reset: unlock and re-sample the DIP pattern. */
			g_system_locked  = false;
			g_pending        = SYSTEM_NONE;
			g_pending_timing = false;
			g_prev_state     = -1;
			if (!terminalIsQuiet()) {
				printk("STATEMACHINE: config-error SW reset -> re-detect\n");
			}
		}
	}
}

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

bool isSoloConfigLatched(void)
{
	return g_solo;
}

static const char *systemName(smSystem_t sys)
{
	switch (sys) {
	case SYSTEM_S1: return "S1";
	case SYSTEM_S2: return "S2";
	default:        return "none (invalid dip)";
	}
}

/* ==================== Fan PWM ==================== */
/* Both state machines keep the same numbering for T0..T4 (see sm_s1.h/sm_s2.h),
 * so one test covers S1 and S2 alike. */
BUILD_ASSERT((int)SM_S1_STANDBY == (int)SM_S2_STANDBY &&
	     (int)SM_S1_RUN == (int)SM_S2_RUN &&
	     (int)SM_S1_UPS == (int)SM_S2_UPS &&
	     (int)SM_S1_SHUTDOWN == (int)SM_S2_SHUTDOWN,
	     "S1 and S2 state numbering must match for the fan logic");

static bool g_fan_on;   /* last applied value; the PWM is only touched on change */

/* T2 RUN and T3 UPS are the powered-on states */
static void fanPwmApply(bool on)
{
	for (uint8_t i = 0U; i < bspPwmGetCount(); i++) {
		if (on) {
			bspPwmSetDutyCycle(i, SM_FAN_PWM_DUTY);
			bspPwmStart(i);
		} else {
			bspPwmStop(i);   /* 0 % */
		}
	}
}

/* ==================== Lifecycle ==================== */

void stateMachineInit(void)
{
	g_system = SYSTEM_NONE;
	g_system_locked = false;
	g_solo = false;
	g_prev_reset = false;
	g_prev_state = -1;

	printk("STATEMACHINE: init\n");
}

void stateMachineTick(void)
{
	const uint32_t din = bspDinGetBitmap();   /* one input snapshot per tick */

	/* Main-mode decision (S1/S2 + solo):
	 *   - The DIP pattern is sampled once per power-on/reset and latched. While
	 *     the unit is powered on a DIP change keeps the current mode (no live
	 *     switch); a new pattern only takes effect after a reset / power cycle.
	 *   - A candidate must hold SYSTEM_CONFIRM_MS before it is latched. A
	 *     conflicting pattern (both active / both inactive / solo with S1)
	 *     latches the invalid error state.
	 *   - SYSTEM_RESET rising edge re-arms the decision, so a corrected DIP also
	 *     takes effect after a reset without a power cycle. */
	const bool cfg_s1   = (din & BIT(DIN_S1_SYSTEM_CONFIG))   != 0U;
	const bool cfg_s2   = (din & BIT(DIN_S2_SYSTEM_CONFIG))   != 0U;
	const bool cfg_solo = (din & BIT(DIN_SOLO_SYSTEM_CONFIG)) != 0U;
	const bool reset    = isResetActive(din);

	if (reset && !g_prev_reset && g_system_locked) {
		g_system_locked  = false;      /* reset -> re-sample the DIP pattern */
		g_pending        = SYSTEM_NONE;
		g_pending_timing = false;
		if (!terminalIsQuiet()) {
			printk("STATEMACHINE: reset -> re-detect\n");
		}
	}
	g_prev_reset = reset;

	if (!g_system_locked) {
		/* Decision:
		 *   s1 && !s2 && !solo -> S1
		 *   !s1 &&  s2         -> S2 (solo selects solo/classic inside S2)
		 *   anything else (both active / both inactive / s1 with solo) -> invalid
		 * NOTE: solo additionally vetoes S1 (avoids S1/solo coupling); if solo turns
		 *       active inside the 1 s window, 'want' becomes NONE and the timer restarts. */
		smSystem_t want;

		if (cfg_s1 && !cfg_s2 && !cfg_solo) {
			want = SYSTEM_S1;
		} else if (!cfg_s1 && cfg_s2) {
			want = SYSTEM_S2;
		} else {
			want = SYSTEM_NONE;
		}

		if (want != g_pending || !g_pending_timing) {
			g_pending        = want;     /* new candidate: restart the 1 s timer */
			g_pending_timing = true;
			g_pending_ms     = k_uptime_get();
		} else if ((k_uptime_get() - g_pending_ms) >= SYSTEM_CONFIRM_MS) {
			const smSystem_t prev      = g_system;
			const bool       solo_prev = g_solo;

			g_system        = want;      /* held 1 s -> latch (NONE = error) */
			g_solo          = cfg_solo;  /* solo is latched with the main mode */
			g_system_locked = true;

			/* Print the mode before entering the sub-state so the log order is
			 * "STATEMACHINE: system -> S1" then "S1: -> STANDBY ..." */
			if (!terminalIsQuiet()) {
				printk("STATEMACHINE: system -> %s\n", systemName(want));
			}

			/* Re-enter only when the configuration really changed: after a reset /
			 * shutdown that keeps the same mode, the sub-state machine keeps its
			 * own state and must not be restarted. */
			if (want != prev || (want == SYSTEM_S2 && cfg_solo != solo_prev)) {
				switch (want) {
				case SYSTEM_S1: smS1Enter(); break;
				case SYSTEM_S2: smS2Enter(); break;
				default:        break;   /* invalid DIP: latched error state */
				}
			}
		}
	}

	/* Trolley debounce (once per tick; sm_s1/sm_s2 read it via the API) */
	trolleyDebounce(din);

	int state = -1;   /* current sub-state of this tick */

	switch (g_system) {
	case SYSTEM_S1:
		state = (int)smS1Tick(din);
		break;

	case SYSTEM_S2:
		state = (int)smS2Tick(din);
		break;

	case SYSTEM_NONE:
		/* Invalid/conflicting DIP (latched) or decision not finished: drive
		 * nothing (safe state). */
		doutWrite(SM_RELAY_ALL, 0);
		doutWrite(SM_LED_ALL, 0);
		doutWrite(SM_DRV_ALL, 0);

		/* A >= 5 s on/off hold is a software reset (re-samples the DIP). */
		configErrorOnOffTick(din);

		if (g_system_locked) {
			/* Latched config error: breathe at the on/off-key feedback rate
			 * (double frequency), so it is clearly distinct from the normal
			 * standby breathing. Only a reset / power cycle leaves this state. */
			indicatorSetMode(INDICATOR_BREATH);
			indicatorSetBreathFast(true);
		}
		break;
	}

	/* ---- Fan PWM follows the on/off state ----
	 * Powered on (T2 RUN / T3 UPS) -> SM_FAN_PWM_DUTY; standby (T0), no mains
	 * (T1), shutdown (T4) and the reset states -> no output. Applied on change
	 * only; bspPwmInit() already leaves every channel at 0 % after reset. */
	const bool fan_on = (state == (int)SM_S1_RUN) || (state == (int)SM_S1_UPS);

	if (fan_on != g_fan_on) {
		g_fan_on = fan_on;
		fanPwmApply(fan_on);
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

	/* ---- Shutdown (T4) re-arms the DIP decision ----
	 * A DIP change while the unit is powered on must not switch the system;
	 * once it has been shut down the configuration is sampled again, so the next
	 * power-on uses the new pattern (or reports a config error). */
	const bool shutdown = (state == (int)SM_S1_SHUTDOWN) ||
			      (state == (int)SM_S2_SHUTDOWN);

	if (shutdown && g_prev_state != state && g_system_locked) {
		g_system_locked  = false;
		g_pending        = SYSTEM_NONE;
		g_pending_timing = false;
		if (!terminalIsQuiet()) {
			printk("STATEMACHINE: shutdown -> re-detect\n");
		}
	}
	g_prev_state = state;
}
