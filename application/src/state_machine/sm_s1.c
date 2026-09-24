/*!
 * @file sm_s1.c
 * @brief S1 state machine implementation (see sm_s1.h)
 *
 * Pin handling: each sub-state writes its own complete level via the BSP bitmap
 * API - no intermediate state table, no generic "apply" wrapper:
 *
 *   doutWrite(SM_RELAY_ALL, S1_STANDBY_RELAY);   // relays: listed on, the rest off
 *   doutWrite(SM_LED_ALL, ...);                  // panel LEDs: listed on, the rest off
 *   doutWrite(SM_DRV_ALL, ...);                  // status drivers: listed on, the rest off
 *
 * A state never "resets the whole group and then re-sets": relays it keeps
 * closed (e.g. K3/K10 in T0/T2/T4) are not dropped, only the ones it does not
 * need are explicitly written 0.
 * K2~K7 are AC relays; K8_1/K8_2/K9~K13 are efuses.
 *
 * WDI (PH9) belongs to the max6703a module and is never touched here.
 */
/*----------------------------------------------------------------------------*/

/* Zephyr */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* Application */
#include "indicator.h"       
#include "state_machine.h"   
#include "sm_s1.h"

/* State names (for logs in this file) */
static const char *s1StateName(smS1State_t st);

/* ==================== Per-state relay sets ====================
 * The relays each state closes are listed explicitly here (md Relay sequence):
 *   K2~K7            = AC relays
 *   K8_1/K8_2/K9~K13 = efuse
 */
/* Standby loop (md T0). K9/K11 (IS_PC_ON) and K13 (APP_HOST_ON) are added
 * conditionally in s1OutputStandby. */
#define S1_STANDBY_RELAY      (K3 | K10)
/* Run (md T2). K4/K5 are not in the set; s1OutputRunTrolley drives them per trolley. */
#define S1_RUN_RELAY          (K3 | K6 | K7 | \
			       K8_1 | K9 | K10 | K11 | K12 | K13)
/* UPS mode (md T3: mains lost after power-on): base set K2,K10; K9/K11 and K13
 * are added conditionally in s1OutputUps. */
#define S1_UPS_RELAY       (K2 | K10)
/* Normal shutdown (md T4): base K3/K10; K9/K11 and K13 are conditional. */
#define S1_SHUTDOWN_RELAY     (K3 | K10)    /* T4: K3,K10; K13 off */

/* ==================== Internal state ==================== */

static smS1State_t s1_state = SM_S1_STANDBY;
static int64_t     s1_entry_ms;
static uint32_t    s1_prev_din;
static bool        s1_prev_valid;   /* first tick: no edge detection */

/* On/off key (SYSTEM_ON_OFF1) hold thresholds (ms):
 *   500 ms ~ 5 s released -> onoff_2s  : power on / off (acts on release)
 *   >= 5 s (still held)   -> onoff_long: software reset
 * One press performs at most one on/off (acted). */
#define S1_ONOFF_MIN_MS     500   /* >= 500 ms: armed, acts on release */
#define S1_ONOFF_RESET_MS  5000   /* >= 5 s: software reset (on release) */

/* Software reset: all off, then T0/T1 after this hold time */
#define S1_SW_RESET_MS 1000      /* SW_RESET dwell time */

/* Hardware reset: hold after SYSTEM_RESET release, then T0 if mains OK */
#define S1_RESET_HOLD_MS 2000

/* No shutdown within 60 s after power-on in T2 (SW reset only) */
#define S1_RUN_NO_OFF_MS 60000

static bool    s1_onoff_pressed;      /* key currently pressed */
static int64_t s1_onoff_start_ms;     /* press start time */
static bool    s1_onoff_2s_fired;     /* armed at 500 ms (acts on release) */
static bool    s1_onoff_reset_armed;  /* armed at 5 s (reset on release) */
static bool    s1_onoff_acted;        /* already performed on/off this press */

/* K4/K5 follow trolley with a 10 ms step: 0=off, 1=K4 closed, 2=K4+K5 closed */
#define S1_K45_STEP_MS 10
static uint8_t s1_k45_state;
static int64_t s1_k45_ms;

/* IS_PC_ON / APP_HOST_ON latch-off (shared by T0/T3/T4): once an input goes low
 * it is latched off; re-sampled on the first call after entering T0/T3/T4. */
static bool s1_od_active;      /* T0/T3/T4 entry re-sampled */
static bool s1_ispc_off;
static bool s1_apphost_off;

/* Re-sample the IS_PC_ON / APP_HOST_ON latch on state entry, then latch any low */
static void s1UpdateOdLatch(uint32_t din)
{
	if (!s1_od_active) {
		s1_od_active      = true;
		s1_ispc_off       = false;
		s1_apphost_off    = false;
	}
	if (!isPcOn(din)) {
		s1_ispc_off = true;
	}
	if (!isAppHostOn(din)) {
		s1_apphost_off = true;
	}
}

/* Latched-on condition: input high and never seen low since state entry */
static bool s1PcLatched(uint32_t din)
{
	return isPcOn(din) && !s1_ispc_off;
}

static bool s1AppHostLatched(uint32_t din)
{
	return isAppHostOn(din) && !s1_apphost_off;
}

static bool s1_t2_rep_valid;   /* T2 trolley report cache */
static bool s1_t2_rep_trolley;

/* Relay log: print the K set of the current state (only when it changes) */
static const struct { uint64_t bit; const char *name; } s1_k_tab[] = {
	{ K2, "K2" }, { K3, "K3" }, { K4, "K4" }, { K5, "K5" }, { K6, "K6" },
	{ K7, "K7" }, { K8_1, "K8_1" }, { K8_2, "K8_2" }, { K9, "K9" },
	{ K10, "K10" }, { K11, "K11" }, { K12, "K12" }, { K13, "K13" },
};

static void s1RelayLog(const char *stage, uint64_t mask)
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

	for (size_t i = 0; i < ARRAY_SIZE(s1_k_tab); i++) {
		if ((mask & s1_k_tab[i].bit) != 0U) {
			off += (size_t)snprintk(buf + off, sizeof(buf) - off, "%s ",
						s1_k_tab[i].name);
			if (off >= sizeof(buf)) {
				break;
			}
		}
	}
	if (off == 0U) {
		snprintk(buf, sizeof(buf), "(none)");
	}

	printk("S1 %s relay: %s\n", stage, buf);
}

/* ==================== Per-state output functions ==================== */

/* 24V check: print the reading (scaled mV + raw ADC) and return pass/fail */
static bool s1Check24V(const char *stage)
{
	const uint32_t mv = sensorGetPhys(SM_24V_AIN_CH);
	const bool     ok = is24VOk();

	printk("S1: %s 24V check %u mV (raw %u, threshold %u mV) -> %s\n",
	       stage, mv, bspAinGetRawValue(SM_24V_AIN_CH), SM_24V_MIN_MV,
	       ok ? "OK" : "FAIL");

	return ok;
}

/* STANDBY (md T0; identically the "UPS mode, mains restored" result of md
 * section 7, because mains restore leads here):
 *   relays  K3,K10 + K9/K11 (IS_PC_ON) + K13 (APP_HOST_ON)
 *   LEDs    GRID / S1_SYS_ON / PWR24 / CP24 / PAC230V (+ trolley)
 *   drivers MAINS_CONNECTED_MCU/IS_PC (+ trolley); the IS_PC/APP_HOST "site"
 *           drivers themselves are only enabled from T2/T3 on */
static void s1OutputStandby(uint32_t din)
{
	uint64_t relay = S1_STANDBY_RELAY;                  /* K3 | K10 */
	uint64_t led   = LED_GRID_PWR_IN | LED_S1_SYS_ON | LED_PWR24V_ON |
			 LED_CP24V_ON | LED_PAC230V_ON;
	uint64_t drv   = DRV_MAINS_CONNECTED_MCU | DRV_MAINS_CONNECTED_IS_PC;

	s1UpdateOdLatch(din);

	if (s1PcLatched(din)) {
		relay |= (K9 | K11);
	}
	if (s1AppHostLatched(din)) {
		relay |= K13;
	}

	/* Trolley LED/drivers are only output while powered on (T2/T3); they are
	 * not driven in standby (T0). */

	doutWrite(SM_RELAY_ALL, relay);
	s1RelayLog("T0", relay);
	doutWrite(SM_LED_ALL, led);
	doutWrite(SM_DRV_ALL, drv);
	indicatorSetMode(INDICATOR_BREATH);   /* standby -> breathing */
}

/* OFF_NO_MAINS (md T1; md section 8 says the UPS-mode power-off closes every
 * relay / signal / efuse enable and then waits for the supply to drop) */
static void s1OutputOffNoMains(void)
{
	doutWrite(SM_RELAY_ALL, 0);   /* all relays / efuse enables off */
	s1RelayLog("T1", 0);
	doutWrite(SM_LED_ALL, 0);
	doutWrite(SM_DRV_ALL, 0);
	indicatorSetMode(INDICATOR_OFF);
}

/* T2 outputs (LEDs + drivers): fixed part plus the trolley-conditional part
 * (md T2 note). Called every tick to keep following the trolley. */
static void s1OutputRunTrolley(uint32_t din)
{
	const bool trolley = isTrolleyConnectedDebounced();

	/* T2 trolley report: print only on change (this runs every 1 ms) */
	if (!s1_t2_rep_valid || s1_t2_rep_trolley != trolley) {
		s1_t2_rep_valid   = true;
		s1_t2_rep_trolley = trolley;
		printk("S1 T2: trolley=%d\n", trolley);
	}
	uint64_t led = LED_GRID_PWR_IN | LED_S1_SYS_ON | LED_SYS_ON | LED_PWR24V_ON | LED_CP24V_ON | LED_PAC230V_ON;
	uint64_t drv = DRV_MAINS_CONNECTED_MCU | DRV_MAINS_CONNECTED_IS_PC |
		       DRV_IS_PC_SITE | DRV_APP_HOST;

	if (trolley) {
		led |= LED_TROLLEY_CONNECTED;
		drv |= (DRV_TROLLEY_EN | DRV_TRL_MU_MCU | DRV_TRL_MU_IS_PC);
	}
	else{
		led &= ~LED_TROLLEY_CONNECTED;
		drv &= ~(DRV_TROLLEY_EN | DRV_TRL_MU_MCU | DRV_TRL_MU_IS_PC);
	}

	doutWrite(SM_LED_ALL, led);
	doutWrite(SM_DRV_ALL, drv);

	/* K4/K5 follow trolley (K4 first, K5 10 ms later; open immediately on loss).
	 * They are not part of S1_RUN_RELAY and are driven here. */
	if (!trolley) {
		bspDoutSetBitmap(K4 | K5, false);   /* not connected -> open K4/K5 */
		s1_k45_state = 0U;
		return;
	}


	if (s1_k45_state == 0U) {
		bspDoutSetBitmap(K4, true);
		s1_k45_state = 1U;
		s1_k45_ms = k_uptime_get();
	} else if (s1_k45_state == 1U &&
		   (k_uptime_get() - s1_k45_ms) >= S1_K45_STEP_MS) {
		bspDoutSetBitmap(K5, true);
		s1_k45_state = 2U;
	}
}

/* RUN (md T2): enable S1_RUN_RELAY, disable the rest (K2/K4/K5 handled by
 * s1OutputRunTrolley). */
static void s1OutputRun(uint32_t din)
{
	doutWriteKeep(SM_RELAY_ALL, S1_RUN_RELAY, K4 | K5);
	s1RelayLog("T2", S1_RUN_RELAY);
	s1OutputRunTrolley(din);
	indicatorSetMode(INDICATOR_ON);   /* running -> solid on */
}

/* UPS mode (md T3: mains lost after power-on). Base S1_UPS_RELAY (K2,K10) plus
 * the conditional outputs (same one-way latch as T0/T4):
 *   IS_PC_ON    high -> K9 | K11 + DRV_IS_PC_SITE ; low -> all three off
 *   APP_HOST_ON high -> K13      + DRV_APP_HOST   ; low -> both off
 * LED_SYSTEM_ON is enabled in T3 as well (same as T2). */
static void s1OutputUps(uint32_t din)
{
	const bool trolley = isTrolleyConnectedDebounced();
	uint64_t relay = S1_UPS_RELAY;                   /* K2 | K10 */
	uint64_t drv   = 0;
	uint64_t led   = LED_UPS_IN | LED_S1_SYS_ON | LED_SYS_ON | LED_PWR24V_ON |
			 LED_CP24V_ON | LED_PAC230V_ON;

	s1UpdateOdLatch(din);

	if (s1PcLatched(din)) {
		relay |= (K9 | K11);
		drv   |= DRV_IS_PC_SITE;
	}
	if (s1AppHostLatched(din)) {
		relay |= K13;
		drv   |= DRV_APP_HOST;
	}

	/* Powered-on state: trolley LED/drivers follow the (debounced) trolley. */
	if (trolley) {
		led |= LED_TROLLEY_CONNECTED;
		drv |= DRV_TROLLEY_EN | DRV_TRL_MU_MCU | DRV_TRL_MU_IS_PC;
	}

	doutWrite(SM_RELAY_ALL, relay);
	s1RelayLog("T3", relay);
	doutWrite(SM_LED_ALL, led);
	doutWrite(SM_DRV_ALL, drv);
	indicatorSetMode(INDICATOR_ON);   /* UPS mode -> solid on */
}

/* T4 input report (trolley / IS_PC_ON / APP_HOST_ON): this function runs every
 * 1 ms, so print only when one of the three changes. */

static bool s1_t4_rep_valid;
static bool s1_t4_rep_trolley;
static bool s1_t4_rep_ispc;
static bool s1_t4_rep_apphost;

static void s1ReportShutdownInputs(uint32_t din)
{
	const bool trolley = isTrolleyConnectedDebounced();
	const bool ispc    = isPcOn(din);
	const bool apphost = isAppHostOn(din);

	if (s1_t4_rep_valid && trolley == s1_t4_rep_trolley &&
	    ispc == s1_t4_rep_ispc && apphost == s1_t4_rep_apphost) {
		return;   /* unchanged -> do not print */
	}
	s1_t4_rep_valid   = true;
	s1_t4_rep_trolley = trolley;
	s1_t4_rep_ispc    = ispc;
	s1_t4_rep_apphost = apphost;

	printk("S1 T4: trolley=%d is_pc_on=%d app_host_on=%d\n", trolley, ispc, apphost);
}

/* SHUTDOWN (md T4): base K3,K10 (K13 off) plus dynamic bits (refreshed every ms)
 *   IS_PC_ON    high -> K9 | K11   (DRV_IS_PC_SITE is only on in the running states)
 *   APP_HOST_ON high -> K13        (DRV_APP_HOST    is only on in the running states)
 *   trolley LED/drivers are NOT driven in T4 (only in the powered-on T2/T3) */
static void s1OutputShutdown(uint32_t din)
{
	uint64_t relay = S1_SHUTDOWN_RELAY;                     /* K3 | K10 (K13 off) */
	uint64_t led   = LED_GRID_PWR_IN | LED_PWR24V_ON | LED_CP24V_ON | LED_PAC230V_ON;
	uint64_t drv   = DRV_MAINS_CONNECTED_MCU | DRV_MAINS_CONNECTED_IS_PC;

	s1ReportShutdownInputs(din);

	/* Trolley LED/drivers are only output while powered on (T2/T3); they are
	 * not driven in shutdown (T4). */

	s1UpdateOdLatch(din);

	/* IS_PC / APP_HOST only affect relays here (K9/K11; K13).
	 * DRV_IS_PC_SITE / DRV_APP_HOST are only enabled in the running states. */
	if (s1PcLatched(din)) {
		relay |= (K9 | K11);
	}
	else {
		relay &= ~(K9 | K11);
	}

	if (s1AppHostLatched(din)) {
		relay |= K13;
	}
	else {
		relay &= ~K13;
	}

	doutWrite(SM_RELAY_ALL, relay);
	s1RelayLog("T4", relay);
	doutWrite(SM_LED_ALL, led);
	doutWrite(SM_DRV_ALL, drv);
	indicatorSetMode(INDICATOR_BREATH);   /* shutdown looks like standby: breathing */
}

/* Software reset: turn everything off (all relays, LEDs, drivers) */
static void s1OutputSwReset(void)
{
	doutWrite(SM_RELAY_ALL, 0);   /* all relays off */
	s1RelayLog("SW_RESET", 0);
	doutWrite(SM_LED_ALL, 0);
	doutWrite(SM_DRV_ALL, 0);
	indicatorSetMode(INDICATOR_OFF);
}

static void s1OutputReset(void)
{
	doutWrite(SM_RELAY_ALL, 0);
	s1RelayLog("RESET", 0);
	doutWrite(SM_LED_ALL, 0);
	doutWrite(SM_DRV_ALL, 0);
	indicatorSetMode(INDICATOR_OFF);
}

/* Write a state's own outputs (also called every 1 ms from smS1Tick: idempotent) */
static void s1Output(smS1State_t st, uint32_t din)
{
	switch (st) {
	case SM_S1_STANDBY:      s1OutputStandby(din);   break;
	case SM_S1_OFF_NO_MAINS: s1OutputOffNoMains();   break;
	case SM_S1_RUN:          s1OutputRun(din);       break;
	case SM_S1_UPS:       s1OutputUps(din);     break;
	case SM_S1_SHUTDOWN:     s1OutputShutdown(din);  break;
	case SM_S1_RESET:        s1OutputReset();        break;
	case SM_S1_SW_RESET:     s1OutputSwReset();     break;
	default:                 						 break;
	}
}

/* ==================== State transitions ==================== */

static void s1EnterState(smS1State_t st, uint32_t din)
{
	s1_state    = st;
	s1_entry_ms = k_uptime_get();
	s1_k45_state      = 0U;   /* restart the K4/K5 sequence */

	printk("S1: -> %s\n", s1StateName(st));

	s1_t2_rep_valid = false;   /* new state: invalidate the T2 trolley report */
	s1_t4_rep_valid = false;
	s1_od_active = false;     /* new state: IS_PC/APP_HOST will be re-sampled */

	/* Write once on entry: RESET returns early in the tick and would never reach
	 * the per-tick s1Output; other states just apply 1 ms earlier (idempotent). */
	s1Output(st, din);
}

/* ==================== Public API ==================== */

static const char *s1StateName(smS1State_t st)
{
	switch (st) {
	case SM_S1_STANDBY:      return "STANDBY (T0)";
	case SM_S1_OFF_NO_MAINS: return "OFF_NO_MAINS (T1)";
	case SM_S1_RUN:          return "RUN (T2)";
	case SM_S1_UPS:       return "UPS (T3)";
	case SM_S1_SHUTDOWN:     return "SHUTDOWN (T4)";
	case SM_S1_RESET:        return "RESET (hardware)";
	case SM_S1_SW_RESET:     return "SW_RESET (software)";
	default:                 return "?";
	}
}

void smS1Enter(void)
{
	s1_prev_valid = false;      /* first tick records inputs without edges */
	s1_prev_din   = 0;
	s1_onoff_pressed    = false;
	s1_onoff_start_ms   = 0;
	s1_onoff_2s_fired   = false;
	s1_onoff_reset_armed = false;
	s1_onoff_acted      = false;

	/* Enter the correct initial sub-state directly: mains OK -> T0, else T1. */
	const uint32_t din = bspDinGetBitmap();

	s1EnterState(isMainsOk(din) ? SM_S1_STANDBY : SM_S1_OFF_NO_MAINS, din);
}

smS1State_t smS1Tick(uint32_t din)
{
	const int64_t now        = k_uptime_get();
	const bool    mains      = isMainsOk(din);
	const bool    onoff      = isOnOffActive(din);
	const bool    onoff_prev = isOnOffActive(s1_prev_din);
	const bool    reset      = isResetActive(din);
	const bool    reset_prev = isResetActive(s1_prev_din);

	bool reset_rise  = reset && !reset_prev;
	bool onoff_2s    = false;   /* released after 500 ms: on/off */
	bool onoff_long  = false;   /* held 5 s: software reset */

	/* ---- On/off key timing (low->high = pressed) ----
	 *   released after >= 500 ms -> onoff_2s  (power on/off, once per press)
	 *   released after >= 5 s     -> onoff_long (software reset)
	 * Both fire on the release edge; >= 5 s only resets, no on/off. */
	if (!s1_prev_valid) {
		reset_rise = false;
		s1_onoff_pressed    = onoff;
		s1_onoff_start_ms   = onoff ? now : 0;
		s1_onoff_2s_fired   = false;
		s1_onoff_reset_armed = false;
		s1_onoff_acted      = false;
		s1_prev_valid = true;
	} else if (onoff && !onoff_prev) {              /* low->high: pressed */
		s1_onoff_pressed    = true;
		s1_onoff_start_ms   = now;
		s1_onoff_2s_fired   = false;
		s1_onoff_reset_armed = false;
		s1_onoff_acted      = false;
	} else if (!onoff && onoff_prev) {              /* high->low: released, act now */
		s1_onoff_pressed = false;
		if (s1_onoff_reset_armed) {
			onoff_long = true;                      /* released after 5 s -> reset */
		} else if (s1_onoff_2s_fired) {
			onoff_2s = true;                        /* released after 500 ms -> on/off */
		}
		s1_onoff_2s_fired    = false;
		s1_onoff_reset_armed = false;
		s1_onoff_acted       = false;
	}

	if (onoff && s1_onoff_pressed) {
		const int64_t held = now - s1_onoff_start_ms;

		if (held >= S1_ONOFF_RESET_MS) {
			s1_onoff_reset_armed = true;            /* 5 s: reset armed */
		} else if (held >= S1_ONOFF_MIN_MS) {
			s1_onoff_2s_fired = true;               /* 500 ms: on/off armed */
		}
	}

	/* Transitions are driven by mains (ME_BOX_ERROR) only. */

	/* ---- 1) Hardware reset first: SYSTEM_RESET rising edge -> full re-init ---- */
	if (reset_rise) {
		s1_onoff_pressed    = false;
		s1_onoff_2s_fired   = false;
		s1_onoff_reset_armed = false;
		s1_onoff_acted      = false;
		s1EnterState(SM_S1_RESET, din);
		s1_prev_din = din;
		return s1_state;
	}

	/* ---- 2) RESET state: 2 s after release, T0 if mains OK, else stay off ---- */
	if (s1_state == SM_S1_RESET) {
		if (!reset && (now - s1_entry_ms) >= S1_RESET_HOLD_MS) {
			s1EnterState(mains ? SM_S1_STANDBY : SM_S1_OFF_NO_MAINS, din);
		}
		s1_prev_din = din;
		return s1_state;
	}

	/* ---- 3) Software reset: key released after >= 5 s (any state) ----
	 * Enter SW_RESET: all outputs off, then T0/T1. */
	if (onoff_long) {
		s1EnterState(SM_S1_SW_RESET, din);
		s1_prev_din = din;
		return s1_state;
	}

	/* ---- 4) SW_RESET state: wait SW_RESET_MS, then T0/T1 by mains ---- */
	if (s1_state == SM_S1_SW_RESET) {
		if ((now - s1_entry_ms) >= S1_SW_RESET_MS) {
			s1EnterState(mains ? SM_S1_STANDBY : SM_S1_OFF_NO_MAINS, din);
		}
		s1_prev_din = din;
		return s1_state;
	}


	/* ---- Transitions ---- */
	switch (s1_state) {

	case SM_S1_STANDBY:   /* T0 standby */
		if (!mains) {
			s1EnterState(SM_S1_OFF_NO_MAINS, din);   /* mains lost -> T1 */
		} else if (onoff_2s && s1Check24V("power-on")) {
			s1_onoff_acted = true;                   /* this press already acted */
			s1EnterState(SM_S1_RUN, din);            /* 24V OK -> power on */
		}
		break;

	case SM_S1_OFF_NO_MAINS:   /* T1 off, mains lost */
		if (mains) {
			s1EnterState(SM_S1_STANDBY, din);        /* mains restored -> T0 */
		}
		break;

	case SM_S1_RUN:   /* T2 running */
		if (!mains) {
			s1EnterState(SM_S1_UPS, din);         /* mains lost -> T3 */
		} else if (onoff_2s && !s1_onoff_acted && s1Check24V("power-off")) {
			s1_onoff_acted = true;                   /* this press already acted */
			s1EnterState(SM_S1_SHUTDOWN, din);       /* 24V OK -> shutdown */
		}
		break;

	case SM_S1_UPS:   /* T3 UPS mode (steady state) */
		/* T3 may only go to T0 or T1:
		 *   mains restored -> T0 (standby; does not auto-resume T2)
		 *   valid power-off -> T1 */
		if (mains) {
			s1EnterState(SM_S1_STANDBY, din);         /* -> T0 */
		} else if (onoff_2s && !s1_onoff_acted && s1Check24V("power-off")) {
			s1_onoff_acted = true;                    /* this press already acted */
			s1EnterState(SM_S1_OFF_NO_MAINS, din);    /* power-off -> T1 */
		}
		break;

	case SM_S1_SHUTDOWN:   /* T4 shutdown (steady; wait for the key to power on) */
		if (!mains) {
			s1EnterState(SM_S1_OFF_NO_MAINS, din);   /* mains lost -> T1 */
		} else if (onoff_2s && !s1_onoff_acted && s1Check24V("power-on")) {
			s1_onoff_acted = true;
			s1EnterState(SM_S1_RUN, din);            /* press again -> T2 */
		}
		break;

	default:
		break;
	}

	/* Re-apply the current state's outputs every 1 ms (idempotent): trolley,
	 * K4/K5 progression and the T4 IS_PC/APP_HOST bits all follow here. */
	s1Output(s1_state, din);

	/* While the on/off key is held: indicator breathes at double frequency */
	indicatorSetBreathFast(onoff);

	/* Global indicators: PD9 = S1 system (always on in S1).
	 * The trolley LED (PD6) is driven per-state (only in T2/T3 now). */
	bspDoutSetBitmap(BIT64(DOUT_LED_S1_SYS_ON), true);

	s1_prev_din = din;
	return s1_state;
}
