/*!
 * @file sm_s2.c
 * @brief S2 state machine implementation (see sm_s2.h)
 *
 * Per S1_MU_SYS_ctr_logic.md:
 *   section 5 "Solo with Trolley"       - sub-mode solo  (pj4 = 1)
 *   section 6 "Chassis SYS with Trolley" - sub-mode classic (pj4 = 0)
 *
 * Same skeleton as S1: T0~T4 + hardware/software reset; transitions by mains only;
 * on/off key: release after 500 ms..5 s = on/off, release after >= 5 s = SW reset.
 *
 * Trolley is followed in T2/T3 (LED_TROLLEY_CONNECTED / TROLLEY_ENABLE_DRV /
 * TRL_MU_*). Pin definitions come from state_machine.h, shared with S1.
 */
/*----------------------------------------------------------------------------*/

/* Zephyr */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* Application */
#include "indicator.h"
#include "state_machine.h"
#include "sm_s2.h"

/* State names (for logs in this file) */
static const char *s2StateName(smS2State_t st);

/* ==================== Per-state relay sets (per the S2 md files) ==================== */

/* T0 standby loop; T1 (off / mains lost) closes everything instead */
#define S2_STANDBY_RELAY        (K3 | K10)
#define S2_SHUTDOWN_RELAY       (K3 | K10)    /* T4: K3,K10; K13 off */
/* T2 run (base set; the trolley-driven relay K5 is handled in
 * s2OutputRunTrolley):
 *   solo    : K3,K4,K6,K7,K8_1,K8_2,K9,K10,K11,K12,K13
 *   classic : K3,K4,K7,K8_1,K8_2,K9,K10,K11,K12,K13 (no K6) */
#define S2_RUN_RELAY_SOLO       (K3 | K4 | K6 | K7 | \
				 K8_1 | K8_2 | K9 | K10 | K11 | K12 | K13)
#define S2_RUN_RELAY_CHASSIS    (K3 | K4 | K7 | \
				 K8_1 | K8_2 | K9 | K10 | K11 | K12 | K13)
/* UPS mode (md T3: mains lost after power-on), base set; same for solo and
 * classic, and K5 follows the trolley */
#define S2_UPS_RELAY         (K2 | K8_1 | K8_2 | K9 | K10 | K11 | K12 | K13)

/* ==================== Internal state ==================== */

static smS2State_t s2_state = SM_S2_STANDBY;
static int64_t     s2_entry_ms;
static uint32_t    s2_prev_din;
static bool        s2_prev_valid;    /* first tick: no edge detection */
static bool        s2_solo;          /* sub-mode: pj4 */

/* On/off key (SYSTEM_ON_OFF), same as S1:
 *   500 ms ~ 5 s released -> on/off; >= 5 s -> software reset */
#define S2_ONOFF_MIN_MS     500   /* >= 500 ms: armed, acts on release */
#define S2_ONOFF_RESET_MS  5000   /* >= 5 s: software reset (on release) */

/* Software reset: all off, then T0/T1 after this hold time */
#define S2_SW_RESET_MS 1000      /* SW_RESET dwell time */

static bool    s2_onoff_pressed;
static int64_t s2_onoff_start_ms;
static bool    s2_onoff_2s_fired;
static bool    s2_onoff_reset_armed;
static bool    s2_onoff_acted;

static bool s2_t2_rep_valid;   /* T2 trolley report cache */
static bool s2_t2_rep_trolley;

#define S2_RESET_HOLD_MS 2000


/* Relay log: print the K set of the current state (only when it changes) */
static const struct { uint64_t bit; const char *name; } s2_k_tab[] = {
	{ K2, "K2" }, { K3, "K3" }, { K4, "K4" }, { K5, "K5" }, { K6, "K6" },
	{ K7, "K7" }, { K8_1, "K8_1" }, { K8_2, "K8_2" }, { K9, "K9" },
	{ K10, "K10" }, { K11, "K11" }, { K12, "K12" }, { K13, "K13" },
};

static void s2RelayLog(const char *stage, uint64_t mask)
{
	static uint64_t prev;
	static bool     valid;
	char buf[96];
	size_t off = 0;

	if (valid && mask == prev) {
		return;   /* unchanged -> do not print */
	}
	valid = true;
	prev  = mask;

	for (size_t i = 0; i < ARRAY_SIZE(s2_k_tab); i++) {
		if ((mask & s2_k_tab[i].bit) != 0U) {
			off += (size_t)snprintk(buf + off, sizeof(buf) - off, "%s ",
						s2_k_tab[i].name);
			if (off >= sizeof(buf)) {
				break;
			}
		}
	}
	if (off == 0U) {
		snprintk(buf, sizeof(buf), "(none)");
	}

	printk("S2 %s relay: %s\n", stage, buf);
}

/* ==================== Per-state output functions ==================== */

/* Sub-mode LEDs: solo = S2_SYS + S2_SOLO_SYS, classic = S2_SYS only */
static uint64_t s2ModeLed(void)
{
	return LED_S2_SYS_ON | (s2_solo ? LED_S2_SOLO_SYS : 0ULL);
}

/* 24V check: print the reading (scaled mV + raw ADC) and return pass/fail */
static bool s2Check24V(const char *stage)
{
	const uint32_t mv = sensorGetPhys(SM_24V_AIN_CH);
	const bool     ok = is24VOk();

	printk("S2: %s 24V check %u mV (raw %u, threshold %u mV) -> %s\n",
	       stage, mv, bspAinGetRawValue(SM_24V_AIN_CH), SM_24V_MIN_MV,
	       ok ? "OK" : "FAIL");

	return ok;
}

/* T0 standby (md T0): K3,K10 + S2_SYS(+SOLO) LEDs + the mains-detect drivers;
 * the trolley signals follow the 2 s debounced trolley state. */
static void s2OutputStandby(uint32_t din)
{
	uint64_t led = s2ModeLed() | LED_GRID_PWR_IN | LED_PWR24V_ON | LED_CP24V_ON |
		       LED_PAC230V_ON;
	uint64_t drv = DRV_MAINS_CONNECTED_MCU | DRV_MAINS_CONNECTED_IS_PC;

	/* Trolley LED/drivers are only output while powered on (T2/T3); they are
	 * not driven in standby (T0). */

	doutWrite(SM_RELAY_ALL, S2_STANDBY_RELAY);
	s2RelayLog("T0", S2_STANDBY_RELAY);
	doutWrite(SM_LED_ALL, led);
	doutWrite(SM_DRV_ALL, drv);
	indicatorSetMode(INDICATOR_BREATH);   /* standby -> breathing */
}

/* OFF_NO_MAINS (T1): close every relay / signal / efuse enable and wait for the
 * supply to drop (same semantics as S1, md section 8) */
static void s2OutputOffNoMains(void)
{
	doutWrite(SM_RELAY_ALL, 0);   /* all relays / efuse enables off */
	s2RelayLog("T1", 0);
	doutWrite(SM_LED_ALL, 0);
	doutWrite(SM_DRV_ALL, 0);
	indicatorSetMode(INDICATOR_OFF);
}

/* T2 outputs (LEDs + drivers): fixed part plus the trolley-conditional part. */
static void s2OutputRunTrolley(uint32_t din)
{
	const bool trolley = isTrolleyConnectedDebounced();

	/* T2 trolley report: print only on change (runs every 1 ms) */
	if (!s2_t2_rep_valid || s2_t2_rep_trolley != trolley) {
		s2_t2_rep_valid   = true;
		s2_t2_rep_trolley = trolley;
		printk("S2 T2: trolley=%d\n", trolley);
	}
	uint64_t led = s2ModeLed() | LED_GRID_PWR_IN | LED_SYS_ON | LED_PWR24V_ON |
		       LED_CP24V_ON | LED_PAC230V_ON;
	/* md T2 (both sub-modes): MAINS_MCU/IS_PC + IS_PC/APP_HOST site drivers */
	uint64_t drv = DRV_MAINS_CONNECTED_MCU | DRV_MAINS_CONNECTED_IS_PC |
		       DRV_IS_PC_SITE | DRV_APP_HOST;

	if (trolley) {
		led |= LED_TROLLEY_CONNECTED;
		drv |= (DRV_TROLLEY_EN | DRV_TRL_MU_MCU | DRV_TRL_MU_IS_PC);
	}

	doutWrite(SM_LED_ALL, led);
	doutWrite(SM_DRV_ALL, drv);

	/* Trolley relay K5 (both sub-modes, closed when connected) */
	bspDoutSetBitmap(K5, trolley);
}

/* RUN (md T2): enable the solo/chassis base set, disable the rest; the trolley
 * relay is driven by s2OutputRunTrolley. */
static void s2OutputRun(uint32_t din)
{
	/* K5 is the trolley relay and is driven by s2OutputRunTrolley */
	const uint64_t relay = s2_solo ? S2_RUN_RELAY_SOLO : S2_RUN_RELAY_CHASSIS;

	doutWriteKeep(SM_RELAY_ALL, relay, K5);
	s2RelayLog("T2", relay);
	s2OutputRunTrolley(din);
	indicatorSetMode(INDICATOR_ON);   /* running -> solid on */
}

/* UPS mode (md T3: mains lost after power-on). Same for solo and classic:
 *   relays  K2, K8_1, K8_2, K9~K13, with K5 following the trolley
 *   LEDs    S2_SYS(+SOLO) / SYSTEM / PWR24 / CP24 / PAC230V (+ trolley)
 *   drivers none, except the trolley signals when connected - the
 *           MAINS_CONNECTED_MCU / MAINS_CONNECTED_IS_PC drivers are NOT
 *           enabled in UPS mode (T3); no IS_PC/APP_HOST site drivers either. */
static void s2OutputUps(uint32_t din)
{
	const bool trolley = isTrolleyConnectedDebounced();
	uint64_t led = s2ModeLed() | LED_GRID_PWR_IN | LED_SYS_ON | LED_PWR24V_ON |
		       LED_CP24V_ON | LED_PAC230V_ON;
	uint64_t drv = 0;   /* no MAINS_CONNECTED_* drivers in UPS mode (T3) */

	if (trolley) {
		led |= LED_TROLLEY_CONNECTED;
		drv |= DRV_TROLLEY_EN | DRV_TRL_MU_MCU | DRV_TRL_MU_IS_PC;
	}

	doutWriteKeep(SM_RELAY_ALL, S2_UPS_RELAY, K5);
	s2RelayLog("T3", S2_UPS_RELAY);
	doutWrite(SM_LED_ALL, led);
	doutWrite(SM_DRV_ALL, drv);
	bspDoutSetBitmap(K5, trolley);
	indicatorSetMode(INDICATOR_ON);
}

/* SHUTDOWN (md T4): base K3,K10 plus dynamic bits
 *   IS_PC_ON    high -> K9 | K11 (DRV_IS_PC_SITE only in the running states)
 *   APP_HOST_ON high -> K5      (DRV_APP_HOST    only in the running states) */
/* T4 input report (trolley / IS_PC_ON / APP_HOST_ON) and the one-way latch:
 * re-sampled on the first call after entering T4. */
static bool s2_t4_active;      /* T4 entry re-sampled */
static bool s2_t4_ispc_off;
static bool s2_t4_apphost_off;

static bool s2_t4_rep_valid;
static bool s2_t4_rep_trolley;
static bool s2_t4_rep_ispc;
static bool s2_t4_rep_apphost;

static void s2ReportShutdownInputs(uint32_t din)
{
	const bool trolley = isTrolleyConnectedDebounced();
	const bool ispc    = isPcOn(din);
	const bool apphost = isAppHostOn(din);

	if (s2_t4_rep_valid && trolley == s2_t4_rep_trolley &&
	    ispc == s2_t4_rep_ispc && apphost == s2_t4_rep_apphost) {
		return;   /* unchanged -> do not print */
	}
	s2_t4_rep_valid   = true;
	s2_t4_rep_trolley = trolley;
	s2_t4_rep_ispc    = ispc;
	s2_t4_rep_apphost = apphost;

	printk("S2 T4: trolley=%d is_pc_on=%d app_host_on=%d\n", trolley, ispc, apphost);
}

/* SHUTDOWN (md T4): base K3,K10 (K13 off) plus dynamic bits (refreshed every ms)
 *   IS_PC_ON    high -> K9       (DRV_IS_PC_SITE is only on in the running states)
 *   APP_HOST_ON high -> K5 | K11 (DRV_APP_HOST    is only on in the running states)
 *   trolley LED/drivers are NOT driven in T4 (only in the powered-on T2/T3) */
static void s2OutputShutdown(uint32_t din)
{
	uint64_t relay = S2_SHUTDOWN_RELAY;                     /* K3 | K10 (K13 off) */
	uint64_t led   = LED_GRID_PWR_IN | LED_PWR24V_ON | LED_CP24V_ON | LED_PAC230V_ON;
	uint64_t drv   = DRV_MAINS_CONNECTED_MCU | DRV_MAINS_CONNECTED_IS_PC;

	s2ReportShutdownInputs(din);

	/* Trolley LED/drivers are only output while powered on (T2/T3); they are
	 * not driven in shutdown (T4). */

	/* First call after entering T4: clear the latch and re-sample the two inputs. */
	if (!s2_t4_active) {
		s2_t4_active      = true;
		s2_t4_ispc_off    = false;
		s2_t4_apphost_off = false;
	}

	/* IS_PC / APP_HOST latch: once the input goes low it is latched off. */
	if (!isPcOn(din)) {
		s2_t4_ispc_off = true;
	}
	if (!isAppHostOn(din)) {
		s2_t4_apphost_off = true;
	}

	/* md T4: IS_PC_ON high -> K9 | K11 ; APP_HOST_ON high -> K5 */
	if (isPcOn(din) && !s2_t4_ispc_off) {
		relay |= (K9 | K11);
	}
	else {
		relay &= ~(K9 | K11);
	}

	if (isAppHostOn(din) && !s2_t4_apphost_off) {
		relay |= K5;
	}
	else {
		relay &= ~K5;
	}

	doutWrite(SM_RELAY_ALL, relay);
	s2RelayLog("T4", relay);
	doutWrite(SM_LED_ALL, led);
	doutWrite(SM_DRV_ALL, drv);
	indicatorSetMode(INDICATOR_BREATH);   /* shutdown looks like standby: breathing */
}

/* Software reset: turn everything off (all relays, LEDs, drivers) */
static void s2OutputSwReset(void)
{
	doutWrite(SM_RELAY_ALL, 0);   /* all relays off */
	s2RelayLog("SW_RESET", 0);
	doutWrite(SM_LED_ALL, 0);
	doutWrite(SM_DRV_ALL, 0);
	indicatorSetMode(INDICATOR_OFF);
}

static void s2OutputReset(void)
{
	doutWrite(SM_RELAY_ALL, 0);
	s2RelayLog("RESET", 0);
	doutWrite(SM_LED_ALL, 0);
	doutWrite(SM_DRV_ALL, 0);
	indicatorSetMode(INDICATOR_OFF);
}

/* Write a state's own outputs (also called every 1 ms from smS2Tick: idempotent) */
static void s2Output(smS2State_t st, uint32_t din)
{
	switch (st) {
	case SM_S2_STANDBY:      s2OutputStandby(din);   break;
	case SM_S2_OFF_NO_MAINS: s2OutputOffNoMains();   break;
	case SM_S2_RUN:          s2OutputRun(din);       break;
	case SM_S2_UPS:       s2OutputUps(din);     break;
	case SM_S2_SHUTDOWN:     s2OutputShutdown(din);  break;
	case SM_S2_RESET:        s2OutputReset();        break;
	case SM_S2_SW_RESET:     s2OutputSwReset();     break;
	default:                                         break;
	}
}

/* ==================== State transitions ==================== */

static void s2EnterState(smS2State_t st, uint32_t din)
{
	s2_state    = st;
	s2_entry_ms = k_uptime_get();

	printk("S2: -> %s\n", s2StateName(st));

	s2_t2_rep_valid = false;   /* new state: invalidate the T2 trolley report */
	s2_t4_rep_valid = false;
	s2_t4_active = false;     /* new state: IS_PC/APP_HOST will be re-sampled */

	s2Output(st, din);
}

/* ==================== Public API ==================== */

static const char *s2StateName(smS2State_t st)
{
	switch (st) {
	case SM_S2_STANDBY:      return "STANDBY (T0)";
	case SM_S2_OFF_NO_MAINS: return "OFF_NO_MAINS (T1)";
	case SM_S2_RUN:          return "RUN (T2)";
	case SM_S2_UPS:       return "UPS (T3)";
	case SM_S2_SHUTDOWN:     return "SHUTDOWN (T4)";
	case SM_S2_RESET:        return "RESET (hardware)";
	case SM_S2_SW_RESET:     return "SW_RESET (software)";
	default:                 return "?";
	}
}

void smS2Enter(void)
{
	const uint32_t din = bspDinGetBitmap();

	s2_prev_valid = false;
	s2_prev_din   = 0;
	s2_onoff_pressed    = false;
	s2_onoff_start_ms   = 0;
	s2_onoff_2s_fired   = false;
	s2_onoff_reset_armed = false;
	s2_onoff_acted      = false;

	/* Sub-mode (pj4): latched by the top-level state machine together with the
	 * main mode; it is not followed live (see state_machine.c). */
	s2_solo = isSoloConfigLatched();
	printk("S2: mode -> %s\n", s2_solo ? "solo" : "classic");

	/* Enter the correct initial state directly: mains OK -> T0, else T1 */
	s2EnterState(isMainsOk(din) ? SM_S2_STANDBY : SM_S2_OFF_NO_MAINS, din);
}

smS2State_t smS2Tick(uint32_t din)
{
	const int64_t now        = k_uptime_get();
	const bool    mains      = isMainsOk(din);
	const bool    reset      = isResetActive(din);
	const bool    onoff      = isOnOffActive(din);
	const bool    onoff_prev = isOnOffActive(s2_prev_din);

	bool reset_rise  = reset && !isResetActive(s2_prev_din);
	bool onoff_2s    = false;   /* released after 500 ms: on/off */
	bool onoff_long  = false;   /* held 5 s: software reset */

	/* ---- On/off key timing (low->high = pressed) ----
	 *   released after >= 500 ms -> onoff_2s  (on/off, once per press)
	 *   released after >= 5 s     -> onoff_long (software reset)
	 * Both fire on the release edge; >= 5 s only resets. */
	if (!s2_prev_valid) {
		reset_rise = false;
		s2_onoff_pressed    = onoff;
		s2_onoff_start_ms   = onoff ? now : 0;
		s2_onoff_2s_fired   = false;
		s2_onoff_reset_armed = false;
		s2_onoff_acted      = false;
		s2_prev_valid = true;
	} else if (onoff && !onoff_prev) {              /* low->high: pressed */
		s2_onoff_pressed    = true;
		s2_onoff_start_ms   = now;
		s2_onoff_2s_fired   = false;
		s2_onoff_reset_armed = false;
		s2_onoff_acted      = false;
	} else if (!onoff && onoff_prev) {              /* high->low: released, act now */
		s2_onoff_pressed = false;
		if (s2_onoff_reset_armed) {
			onoff_long = true;                      /* released after 5 s -> reset */
		} else if (s2_onoff_2s_fired) {
			onoff_2s = true;                        /* released after 500 ms -> on/off */
		}
		s2_onoff_2s_fired    = false;
		s2_onoff_reset_armed = false;
		s2_onoff_acted       = false;
	}

	if (onoff && s2_onoff_pressed) {
		const int64_t held = now - s2_onoff_start_ms;

		if (held >= S2_ONOFF_RESET_MS) {
			s2_onoff_reset_armed = true;            /* 5 s: reset armed */
		} else if (held >= S2_ONOFF_MIN_MS) {
			s2_onoff_2s_fired = true;               /* 500 ms: on/off armed */
		}
	}

	/* ---- Sub-mode (pj4) is latched with the main mode: no live update here ---- */

	/* ---- 1) Hardware reset first: SYSTEM_RESET rising edge -> full re-init ---- */
	if (reset_rise) {
		s2_onoff_pressed    = false;
		s2_onoff_2s_fired   = false;
		s2_onoff_reset_armed = false;
		s2_onoff_acted      = false;
		s2EnterState(SM_S2_RESET, din);
		s2_prev_din = din;
		return s2_state;
	}

	/* ---- 2) RESET state: 2 s after release, T0 if mains OK, else stay off ---- */
	if (s2_state == SM_S2_RESET) {
		if (!reset && (now - s2_entry_ms) >= S2_RESET_HOLD_MS) {
			s2EnterState(mains ? SM_S2_STANDBY : SM_S2_OFF_NO_MAINS, din);
		}
		s2_prev_din = din;
		return s2_state;
	}

	/* ---- 3) Software reset: key released after >= 5 s (any state) ----
	 * Enter SW_RESET: all outputs off, then T0/T1. */
	if (onoff_long) {
		s2EnterState(SM_S2_SW_RESET, din);
		s2_prev_din = din;
		return s2_state;
	}

	/* ---- 4) SW_RESET state: wait SW_RESET_MS, then T0/T1 by mains ---- */
	if (s2_state == SM_S2_SW_RESET) {
		if ((now - s2_entry_ms) >= S2_SW_RESET_MS) {
			s2EnterState(mains ? SM_S2_STANDBY : SM_S2_OFF_NO_MAINS, din);
		}
		s2_prev_din = din;
		return s2_state;
	}


	/* ---- Transitions (mains only) ---- */
	switch (s2_state) {

	case SM_S2_STANDBY:   /* T0 standby */
		if (!mains) {
			s2EnterState(SM_S2_OFF_NO_MAINS, din);
		} else if (onoff_2s && s2Check24V("power-on")) {
			s2_onoff_acted = true;              /* this press already acted */
			s2EnterState(SM_S2_RUN, din);       /* 24V OK -> power on */
		}
		break;

	case SM_S2_OFF_NO_MAINS:   /* T1 off, mains lost */
		if (mains) {
			s2EnterState(SM_S2_STANDBY, din);
		}
		break;

	case SM_S2_RUN:   /* T2 running */
		if (!mains) {
			s2EnterState(SM_S2_UPS, din);    /* mains lost -> T3 */
		} else if (onoff_2s && !s2_onoff_acted && s2Check24V("power-off")) {
			s2_onoff_acted = true;              /* this press already acted */
			s2EnterState(SM_S2_SHUTDOWN, din);  /* 24V OK -> shutdown */
		}
		break;

	case SM_S2_UPS:   /* T3 UPS mode (steady state) */
		/* Only two exits:
		 *   mains restored  -> T2 (back to running)
		 *   valid power-off -> T1, NOT T4. T4 closes the standby loop again
		 *     (S2_SHUTDOWN_RELAY = K3|K10), which is right when shutting down
		 *     with mains present but wrong here: without mains the unit has to
		 *     lose power, so everything must be closed (md: "UPS mode power-off
		 *     -> close every relay / signal / efuse"). Same as S1. */
		if (mains) {
			s2EnterState(SM_S2_RUN, din);
		}
		else if (onoff_2s && !s2_onoff_acted && s2Check24V("power-off")) {
			s2_onoff_acted = true;                   /* this press already acted */
			s2EnterState(SM_S2_OFF_NO_MAINS, din);   /* power-off -> T1 (all off) */
		}
		break;

	case SM_S2_SHUTDOWN:   /* T4 shutdown (steady; wait for the key) */
		if (!mains) {
			s2EnterState(SM_S2_OFF_NO_MAINS, din);   /* mains lost -> T1 */
		} else if (onoff_2s && !s2_onoff_acted && s2Check24V("power-on")) {
			s2_onoff_acted = true;
			s2EnterState(SM_S2_RUN, din);            /* press again -> T2 */
		}
		break;

	default:
		break;
	}

	/* Re-apply the current state's outputs every 1 ms (idempotent). */
	s2Output(s2_state, din);

	/* While the on/off key is held: indicator breathes at double frequency */
	indicatorSetBreathFast(onoff);

	/* Global indicators: PD10 = S2 system; PD5 = solo.
	 * The trolley LED (PD6) is driven per-state (only in T2/T3 now). */
	bspDoutSetBitmap(BIT64(DOUT_LED_S2_SYS_ON), true);
	bspDoutSetBitmap(BIT64(DOUT_LED_S2_SOLO_SYS), s2_solo);

	s2_prev_din = din;
	return s2_state;
}
