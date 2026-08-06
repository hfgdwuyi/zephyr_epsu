/*
 * * state_machine.c
 *
 * ePSU Power Supply State Machine — cios-zhong implementation.
 *
 * State transitions and relay/LED/fan sequencing per ePSU timing diagram.
 * Tick rate: 1 ms.
 *
 * Layer split:
 *   state_machine   — state transitions, relay control (K3-K12, pwr_on_off,
 *                      trolley_enable), panel LEDs, and fan-speed policy
 *   ntc_sensor      — NTC temperature service (sampling + over-temp detect)
 *   bsp_pwm         — fan PWM control
 *   bsp_aout        — pwr_on_off DAC output
 */

/* C standard library */
#include <string.h>

/* Zephyr */
#include <zephyr/sys/printk.h>

/* BSP */
#include "bsp_aout.h"
#include "bsp_dio.h"
#include "bsp_led.h"
#include "bsp_pwm.h"

/* Application */
#include "ntc_sensor.h"
#include "state_machine.h"

/* ==================== Timing constants (ms) ==================== */

typedef enum {
	SM_TIMING_STARTUP_DELAY      = 100,
	SM_TIMING_K3_WAIT            = 200,
	SM_TIMING_K3_STABLE          = 500,
	SM_TIMING_RELAY_STEP         = 50,
	SM_TIMING_FAULT_RECOVER      = 3000,
	SM_TIMING_RESET_HOLD         = 2000,
	SM_TIMING_CHECKS_PERIOD      = 50,
	SM_TIMING_CONFIG_DEBOUNCE    = 20,
	SM_TIMING_LED_BLINK_FAULT    = 500,
	SM_TIMING_LED_BLINK_NORMAL   = 1000,
	SM_TIMING_FAN_SPIN_UP        = 100,
	SM_TIMING_TEMP_POLL          = 1000,   /* poll NTC every 1000 ms */
	SM_TIMING_TEMP_FAULT_DELAY   = 5000,   /* 5s over-temp persist before fault */
	SM_TIMING_ONOFF_TOGGLE_MS    = 550,    /* on_off rising edge 0.55s → toggle */
	SM_TIMING_ONOFF_RESET_MS     = 5000,   /* on_off falling edge 5s → reset */
	SM_TIMING_RESET_MS           = 1000,   /* external reset rising edge 1s → reset */
} stateMachineTiming_t;

/* ==================== Fan duty settings (PWM percent) ==================== */

typedef enum {
	FAN_DUTY_OFF      = 0,    /* fans off */
	FAN_DUTY_MIN      = 10,   /* spec: minimum fan duty */
	FAN_DUTY_S2_MODE  = 30,   /* low power, minimal cooling */
	FAN_DUTY_MID      = 40,
	FAN_DUTY_NORMAL   = 50,
	FAN_DUTY_HIGH     = 60,
	FAN_DUTY_CHARGING = 70,   /* charging needs extra cooling */
	FAN_DUTY_WARN     = 80,
	FAN_DUTY_MAX      = 100,
} fanDuty_t;

/* ==================== DOUT bit-masks ====================
 * Groups of DOUT pins driven as one set. Bit-masks (rather than
 * index ranges) stay correct even if unrelated pins sit in between. */

#define RELAY_MASK_K3        (BIT(DOUT_K3_1_DRV) | BIT(DOUT_K3_2_DRV))
#define RELAY_MASK_K4_TO_K12 (RELAY_MASK_K3      | BIT(DOUT_K4_DRV)   | \
                              BIT(DOUT_K5_DRV)   | BIT(DOUT_K6_DRV)   | \
                              BIT(DOUT_K8_1_DRV) | BIT(DOUT_K8_2_DRV) | \
                              BIT(DOUT_K9_DRV)   | BIT(DOUT_K10_DRV)  | \
                              BIT(DOUT_K11_DRV)  | BIT(DOUT_K12_DRV))
#define RELAY_MASK_K5_TO_K12 (BIT(DOUT_K5_DRV)   | BIT(DOUT_K6_DRV)   | \
                              BIT(DOUT_K8_1_DRV) | BIT(DOUT_K8_2_DRV) | \
                              BIT(DOUT_K9_DRV)   | BIT(DOUT_K10_DRV)  | \
                              BIT(DOUT_K11_DRV)  | BIT(DOUT_K12_DRV))
#define RELAY_MASK_ALL       (RELAY_MASK_K3 | RELAY_MASK_K4_TO_K12)

#define PANEL_LED_MASK       (BIT(DOUT_LED_S1_SYS_ON)        | \
                              BIT(DOUT_LED_S2_SYS_ON)        | \
                              BIT(DOUT_DBG_LED0)             | \
                              BIT(DOUT_DBG_LED1)             | \
                              BIT(DOUT_DBG_LED2)             | \
                              BIT(DOUT_LED_PAC230V_ON)       | \
                              BIT(DOUT_LED_GRID_PWR_IN)      | \
                              BIT(DOUT_LED_UPS_IN)           | \
                              BIT(DOUT_LED_SYSTEM_ON)        | \
                              BIT(DOUT_LED_S2_SOLO_SYS)      | \
                              BIT(DOUT_LED_TROLLEY_CONNECTED)| \
                              BIT(DOUT_LED_IS_PC_ON)         | \
                              BIT(DOUT_LED_APPHOST_ON))

/* ==================== Internal state ==================== */

/* volatile on cross-thread-visible state: written by sm_thread, read by
 * status_work / external getters. Prevents compiler caching stale values. */
static volatile stateMachineState_t  g_state         = STATEMACHINE_STATE_INIT;
static stateMachineConfig_t g_config        = STATEMACHINE_CFG_S1;
static volatile stateMachineError_t  g_error         = STATEMACHINE_ERR_NONE;
static volatile uint32_t     g_faults        = 0;
static uint32_t     g_state_ticks   = 0;
static bool         g_stateEntered = false;

/* ==================== DIN hold (edge + duration) detectors ====================
 * Detect a signal staying at a target level for a minimum duration.
 * dinHoldTick() is called every 1 ms tick; it returns true once when the
 * signal has been at `target` for at least `hold` ms (one-shot). */

typedef struct {
	bool     armed;      /* false until an edge away from target is seen */
	uint32_t hold_ms;    /* ms the signal has stayed at target level     */
	bool     fired;      /* one-shot already triggered for this hold     */
} dinHold_t;

static dinHold_t s_onoff_rising;   /* on_off 0->1 held 0.55s -> toggle      */
static dinHold_t s_onoff_falling;  /* on_off 1->0 held 5s   -> reset        */
static dinHold_t s_reset_rising;   /* reset  0->1 held 1s   -> reset        */

/* Call every 1 ms tick with the current signal level. Returns true once when
 * the signal rises to `target` and stays there for `hold` ms. Edge-anchored:
 * it only starts timing after the signal first deviates from `target`, so a
 * power-on level already equal to `target` cannot falsely trigger. */
static bool dinHoldTick(dinHold_t *h, bool level, bool target, uint32_t hold)
{
	if (level == target) {
		/* At target level. Only count once armed (i.e. we saw a deviation). */
		if (h->armed) {
			h->hold_ms++;
			if (!h->fired && h->hold_ms >= hold) {
				h->fired = true;
				return true;
			}
		}
	} else {
		/* Away from target: (re)arm. The next rise to target starts timing. */
		h->hold_ms = 0;
		h->fired   = false;
		h->armed   = true;
	}
	return false;
}

/* Start disarmed: the signal must first deviate from its target before
 * timing begins, so a static high/low at boot cannot fire. */
static void dinHoldInit(dinHold_t *h)
{
	h->armed   = false;
	h->hold_ms = 0;
	h->fired   = false;
}

/* ==================== Forward declarations ==================== */

static void stateEnter(stateMachineState_t s);
static void stateRunInit(void);
static void stateRunSysOn(void);
static void stateRunPilotContact(void);
static void stateRunSwitchOn(void);
static void stateRunNormalOp(void);
static void stateRunS2Mode(void);
static void stateRunCharging(void);
static void stateRunShutdown(void);
static void stateRunFault(void);
static void stateRunReset(void);
static void stateRunOff(void);

static stateMachineConfig_t readConfigSwitches(void);
static bool checkMainsPresent(void);
static void panelLedsOff(void);
static void panelLedsUpdate(void);
static void fanSet(bool on, fanDuty_t duty_percent);
static void transitionTo(stateMachineState_t s);
static bool dinHoldTick(dinHold_t *h, bool level, bool target, uint32_t hold);
static void dinHoldInit(dinHold_t *h);

/* ==================== Public API ==================== */

void stateMachineInit(void)
{
	g_state          = STATEMACHINE_STATE_INIT;
	g_error          = STATEMACHINE_ERR_NONE;
	g_faults         = 0;
	g_state_ticks    = 0;
	g_stateEntered  = false;
	g_config         = readConfigSwitches();

	ntcTempInit();

	/* Start disarmed so a static power-on level cannot fire. */
	dinHoldInit(&s_onoff_rising);
	dinHoldInit(&s_onoff_falling);
	dinHoldInit(&s_reset_rising);

	printk("STATEMACHINE: init cfg=%d\n", (int)g_config);
}

void stateMachineTick(void)
{
	g_state_ticks++;

	if (!g_stateEntered) {
		stateEnter(g_state);
		g_stateEntered = true;
	}

	/* DIN hold detection: on_off toggle/reset, external reset.
	 * Runs every tick; actions depend on the current state. */
	bool onoff = bspDinGet(DIN_SYSTEM_ON_OFF);

	if (dinHoldTick(&s_onoff_rising, onoff, true, SM_TIMING_ONOFF_TOGGLE_MS)) {
		/* on_off 0->1 held 0.55s: toggle system on/off */
		if (g_state == STATEMACHINE_STATE_OFF) {
			if (checkMainsPresent()) {
				transitionTo(STATEMACHINE_STATE_INIT);
			}
		} else if (g_state != STATEMACHINE_STATE_FAULT &&
			   g_state != STATEMACHINE_STATE_RESET &&
			   g_state != STATEMACHINE_STATE_SHUTDOWN) {
			transitionTo(STATEMACHINE_STATE_SHUTDOWN);
		}
	}

	/* Resets are gated off the OFF state: while powered off, a low on_off
	 * or a reset press must not auto-restart the system. */
	if (g_state != STATEMACHINE_STATE_OFF) {
		if (dinHoldTick(&s_onoff_falling, onoff, false, SM_TIMING_ONOFF_RESET_MS) ||
		    dinHoldTick(&s_reset_rising, bspDinGet(DIN_SYSTEM_RESET), true, SM_TIMING_RESET_MS)) {
			/* on_off 1->0 held 5s, or external reset 0->1 held 1s */
			g_error = STATEMACHINE_ERR_NONE;
			stateMachineRequestReset();
		}
	}

	switch (g_state) {
	case STATEMACHINE_STATE_INIT:          stateRunInit();          break;
	case STATEMACHINE_STATE_SYS_ON:        stateRunSysOn();        break;
	case STATEMACHINE_STATE_PILOT_CONTACT: stateRunPilotContact(); break;
	case STATEMACHINE_STATE_SWITCH_ON:     stateRunSwitchOn();     break;
	case STATEMACHINE_STATE_NORMAL_OP:     stateRunNormalOp();     break;
	case STATEMACHINE_STATE_S2_MODE:       stateRunS2Mode();       break;
	case STATEMACHINE_STATE_CHARGING:      stateRunCharging();      break;
	case STATEMACHINE_STATE_SHUTDOWN:      stateRunShutdown();      break;
	case STATEMACHINE_STATE_FAULT:         stateRunFault();         break;
	case STATEMACHINE_STATE_RESET:         stateRunReset();         break;
	case STATEMACHINE_STATE_OFF:           stateRunOff();           break;
	default:
		g_state = STATEMACHINE_STATE_FAULT;
		break;
	}
}

stateMachineState_t stateMachineGetState(void)           { return g_state; }
uint32_t   stateMachineGetFaults(void)           { return g_faults; }
stateMachineError_t stateMachineGetError(void)           { return g_error; }

const char *stateMachineGetErrorStr(stateMachineError_t err)
{
	switch (err) {
	case STATEMACHINE_ERR_NONE:            return "OK";
	case STATEMACHINE_ERR_INIT_FAIL:       return "ESLR.35 init error";
	case STATEMACHINE_ERR_K3_TIMEOUT:      return "ESTP3.36 K3 close timeout";
	case STATEMACHINE_ERR_SWITCHON_FAIL:   return "ESIC1.37 switchOn fail";
	case STATEMACHINE_ERR_MAINS_LOSS:      return "ESTP1.38 mains PWR failure";
	case STATEMACHINE_ERR_RESET_RECOVERY:  return "EAKO3.39 reset after error";
	case STATEMACHINE_ERR_CHARGING_FAIL:   return "ESIC.40 charging control err";
	default:                      return "unknown";
	}
}

void stateMachineRequestShutdown(void)
{
	if (g_state == STATEMACHINE_STATE_NORMAL_OP ||
	    g_state == STATEMACHINE_STATE_S2_MODE ||
	    g_state == STATEMACHINE_STATE_CHARGING) {
		printk("STATEMACHINE: shutdown requested\n");
		g_state = STATEMACHINE_STATE_SHUTDOWN;
		g_stateEntered = false;
		g_state_ticks = 0;
	}
}

void stateMachineRequestReset(void)
{
	printk("STATEMACHINE: reset requested\n");
	g_state = STATEMACHINE_STATE_RESET;
	g_stateEntered = false;
	g_state_ticks = 0;
}

void stateMachineRequestCharging(void)
{
	if (g_state == STATEMACHINE_STATE_NORMAL_OP) {
		printk("STATEMACHINE: charging requested\n");
		g_state = STATEMACHINE_STATE_CHARGING;
		g_stateEntered = false;
		g_state_ticks = 0;
	}
}

/* ==================== Helpers ==================== */

static void setError(stateMachineError_t err)
{
	g_error = err;
	g_faults |= (uint32_t)err;
	g_state = STATEMACHINE_STATE_FAULT;
	g_stateEntered = false;
	g_state_ticks = 0;
}

static void transitionTo(stateMachineState_t s)
{
	g_state = s;
	g_stateEntered = false;
	g_state_ticks = 0;
}

/* ==================== Panel LED control ==================== */

static void panelLedsOff(void)
{
	bspDoutSetMask(PANEL_LED_MASK, false);
}

static void panelLedsUpdate(void)
{

	if (g_state == STATEMACHINE_STATE_INIT) {
		panelLedsOff();
		return;
	}

	bool grid_ok  = bspDinGet(DIN_GRID_MAIN_RELAY_STATUS);  /* PH5: high=valid */
	bool me_err   = !bspDinGet(DIN_ME_BOX_ERROR);            /* PH6: high=normal -> low=fault */
	bool trolley  = bspDinGet(DIN_TROLLEY_CONNECTED);        /* PA0: high=connected */
	bool is_pc    = bspDinGet(DIN_IS_PC_ON);
	bool app_host = bspDinGet(DIN_APP_HOST_ON);
	bool s1       = bspDinGet(DIN_S1_SYSTEM_CONFIG);
	bool s2       = bspDinGet(DIN_S2_SYSTEM_CONFIG);
	bool solo     = bspDinGet(DIN_SOLO_SYSTEM_CONFIG);

	bspDoutSetStatus(DOUT_LED_S1_SYS_ON, s1);
	bspDoutSetStatus(DOUT_LED_S2_SYS_ON, s2);
	bspDoutSetStatus(DOUT_LED_S2_SOLO_SYS, s2 || solo);
	bspDoutSetStatus(DOUT_LED_PAC230V_ON, grid_ok && !me_err);
	bspDoutSetStatus(DOUT_LED_GRID_PWR_IN, grid_ok);
	bspDoutSetStatus(DOUT_LED_UPS_IN, !grid_ok);
	bspDoutSetStatus(DOUT_LED_TROLLEY_CONNECTED, trolley);
	bspDoutSetStatus(DOUT_LED_IS_PC_ON, is_pc);
	bspDoutSetStatus(DOUT_LED_APPHOST_ON, app_host);

	/* led_system_on blinks in normal/charging, solid in others */
	bool sys_led;
	if (g_state == STATEMACHINE_STATE_NORMAL_OP || g_state == STATEMACHINE_STATE_CHARGING) {
		sys_led = ((g_state_ticks % SM_TIMING_LED_BLINK_NORMAL) < (SM_TIMING_LED_BLINK_NORMAL / 2));
	} else {
		sys_led = true;
	}
	bspDoutSetStatus(DOUT_LED_SYSTEM_ON, sys_led && !me_err);
}

static void fanSet(bool on, fanDuty_t duty_percent)
{
	if (on) {
		bspPwmSetDutyCycle(FAN_PWM2, duty_percent);
		bspPwmSetDutyCycle(FAN_PWM1, duty_percent);
		bspPwmStart(FAN_PWM2);
		bspPwmStart(FAN_PWM1);
	} else {
		bspPwmStop(FAN_PWM2);
		bspPwmStop(FAN_PWM1);
	}
}

/* Fan duty cycle derived from the hotter of the two NTC sensors */
static fanDuty_t fanDutyFromTemp(int16_t temp_max)
{
	if (temp_max >= NTC_TEMP_THRESH_FAULT)  return FAN_DUTY_MAX;
	if (temp_max >= NTC_TEMP_THRESH_WARN)   return FAN_DUTY_WARN;
	if (temp_max >= NTC_TEMP_THRESH_FAN_MAX) return FAN_DUTY_HIGH;
	if (temp_max >= NTC_TEMP_THRESH_FAN_MID) return FAN_DUTY_MID;
	return FAN_DUTY_MIN;
}

static void relaysAllOff(void)
{
	bspDoutSetMask(RELAY_MASK_ALL, false);
}

/* ==================== State entry ==================== */

static void stateEnter(stateMachineState_t s)
{
	printk("STATEMACHINE: -> state %d (t=%ums)\n", (int)s, (unsigned)g_state_ticks);

	switch (s) {

	case STATEMACHINE_STATE_INIT:
		bspDoutSetStatus(DOUT_TROLLEY_ENABLE_DRV, false);
		bspAoutSetState(AOUT_PWR_ON_OFF, false);
		relaysAllOff();
		panelLedsOff();
		fanSet(false, FAN_DUTY_OFF);
		bspLedSwitchOff(0);
		bspLedSwitchOff(1);
		break;

	case STATEMACHINE_STATE_SYS_ON:
		bspDoutSetStatus(DOUT_TROLLEY_ENABLE_DRV, true);
		panelLedsUpdate();
		bspLedSwitchOn(0);
		break;

	case STATEMACHINE_STATE_PILOT_CONTACT:
		bspAoutSetState(AOUT_PWR_ON_OFF, true);   /* pwr_on_off: 0-1.5V @ 0.25Hz square wave */
		break;

	case STATEMACHINE_STATE_SWITCH_ON:
		bspDoutSetMask(RELAY_MASK_K3, true);
		break;

	case STATEMACHINE_STATE_NORMAL_OP:
		bspDoutSetMask(RELAY_MASK_K4_TO_K12, true);
		panelLedsUpdate();
		fanSet(true, FAN_DUTY_NORMAL);   /* 50% initial, ramp up over time */
		break;

	case STATEMACHINE_STATE_S2_MODE:
		bspDoutSetMask(RELAY_MASK_K5_TO_K12, false);
		panelLedsUpdate();
		fanSet(true, FAN_DUTY_S2_MODE);   /* low power, minimal cooling */
		break;

	case STATEMACHINE_STATE_CHARGING:
		panelLedsUpdate();
		fanSet(true, FAN_DUTY_CHARGING);   /* charging needs extra cooling */
		break;

	case STATEMACHINE_STATE_SHUTDOWN:
		/* Relays are staged off in stateRunShutdown() — do not open them
		 * all at once here. Fans stay on briefly for cool-down. */
		bspAoutSetState(AOUT_PWR_ON_OFF, false);
		break;

	case STATEMACHINE_STATE_FAULT:
		bspAoutSetState(AOUT_PWR_ON_OFF, false);
		relaysAllOff();
		panelLedsOff();
		fanSet(false, FAN_DUTY_OFF);
		bspLedSwitchOff(0);
		bspLedSwitchOn(1);
		break;

	case STATEMACHINE_STATE_OFF:
		bspDoutSetStatus(DOUT_TROLLEY_ENABLE_DRV, false);
		bspAoutSetState(AOUT_PWR_ON_OFF, false);
		bspDoutSetMask(RELAY_MASK_K3 | BIT(DOUT_K4_DRV), false);
		panelLedsOff();
		fanSet(false, FAN_DUTY_OFF);
		bspLedSwitchOff(0);
		bspLedSwitchOff(1);
		break;

	default:
		break;
	}
}

/* ==================== State runners ==================== */

static void stateRunInit(void)
{
	if (g_state_ticks < SM_TIMING_STARTUP_DELAY) {
		return;
	}

	g_config = readConfigSwitches();

	if (!checkMainsPresent()) {
		return;
	}

	transitionTo(STATEMACHINE_STATE_SYS_ON);
}

static void stateRunSysOn(void)
{
	if (g_state_ticks < SM_TIMING_CONFIG_DEBOUNCE) {
		return;
	}

	g_config = readConfigSwitches();

	if (!checkMainsPresent()) {
		setError(STATEMACHINE_ERR_MAINS_LOSS);
		return;
	}

	transitionTo(STATEMACHINE_STATE_PILOT_CONTACT);
}

static void stateRunPilotContact(void)
{
	if (g_state_ticks < SM_TIMING_K3_WAIT) {
		return;
	}

	/* Verify pwr_on_off feedback: DIN_LED_PWR_24_ON (PC6) */
	if (!bspDinGet(DIN_LED_PWR_24_ON)) {
		if (g_state_ticks > 1000) {
			setError(STATEMACHINE_ERR_K3_TIMEOUT);
		}
		return;
	}

	transitionTo(STATEMACHINE_STATE_SWITCH_ON);
}

static void stateRunSwitchOn(void)
{
	if (g_state_ticks < SM_TIMING_K3_STABLE) {
		return;
	}

	if (!bspDinGet(DIN_GRID_MAIN_RELAY_STATUS)) {
		if (g_state_ticks > 2000) {
			setError(STATEMACHINE_ERR_SWITCHON_FAIL);
		}
		return;
	}

	transitionTo(STATEMACHINE_STATE_NORMAL_OP);
}

static void stateRunNormalOp(void)
{
	if ((g_state_ticks % SM_TIMING_CONFIG_DEBOUNCE) == 0) {
		stateMachineConfig_t cfg = readConfigSwitches();
		if (cfg != g_config) {
			g_config = cfg;
			if (cfg == STATEMACHINE_CFG_S2) {
				transitionTo(STATEMACHINE_STATE_S2_MODE);
				return;
			}
		}
	}

	if ((g_state_ticks % SM_TIMING_CHECKS_PERIOD) == 0) {
		if (!checkMainsPresent()) {
			setError(STATEMACHINE_ERR_MAINS_LOSS);
			return;
		}
		/* on_off toggle is handled centrally in stateMachineTick() */
	}

	/* Periodic panel LED refresh */
	if ((g_state_ticks % SM_TIMING_CHECKS_PERIOD) == 0) {
		panelLedsUpdate();
	}

	/* Temperature polling */
	if ((g_state_ticks % SM_TIMING_TEMP_POLL) == 0) {
		ntcTempUpdate(SM_TIMING_TEMP_POLL);

		/* Sensor fault: report immediately rather than treating as cold,
		 * which would silently disable over-temp protection. */
		if (ntcTempSensorFault()) {
			printk("STATEMACHINE: NTC sensor fault!\n");
			setError(STATEMACHINE_ERR_INIT_FAIL);
			return;
		}

		if (ntcTempOvertempFor() >= SM_TIMING_TEMP_FAULT_DELAY) {
			printk("STATEMACHINE: over-temp fault! Tmax=%d.%d\n",
			       ntcTempGetMax() / 10, ntcTempGetMax() % 10);
			setError(STATEMACHINE_ERR_INIT_FAIL);
			return;
		}

		/* Adjust fan speed based on temperature */
		int16_t hi = ntcTempGetMax();
		if (hi > 0 && g_state_ticks > SM_TIMING_FAN_SPIN_UP) {
			fanSet(true, fanDutyFromTemp(hi));
		}
	}

	/* Ramp fan to full speed after spin-up (only if no temp data yet) */
	if (g_state_ticks == SM_TIMING_FAN_SPIN_UP && ntcTempGetMax() <= 0) {
		fanSet(true, FAN_DUTY_WARN);
	}
}

static void stateRunS2Mode(void)
{
	if (g_state_ticks < 50) {
		return;
	}

	if ((g_state_ticks % SM_TIMING_CHECKS_PERIOD) == 0) {
		stateMachineConfig_t cfg = readConfigSwitches();
		if (cfg != STATEMACHINE_CFG_S2) {
			g_config = cfg;
			transitionTo(STATEMACHINE_STATE_NORMAL_OP);
			return;
		}
		if (!checkMainsPresent()) {
			setError(STATEMACHINE_ERR_MAINS_LOSS);
			return;
		}
		/* on_off toggle is handled centrally in stateMachineTick() */
		panelLedsUpdate();
	}
}

static void stateRunCharging(void)
{
	if (g_state_ticks < 100) {
		return;
	}

	if ((g_state_ticks % SM_TIMING_CHECKS_PERIOD) == 0) {
		stateMachineConfig_t cfg = readConfigSwitches();
		if (cfg != g_config) {
			g_config = cfg;
			transitionTo(STATEMACHINE_STATE_NORMAL_OP);
			return;
		}
		if (!checkMainsPresent()) {
			setError(STATEMACHINE_ERR_CHARGING_FAIL);
			return;
		}
		/* on_off toggle is handled centrally in stateMachineTick() */

		/* Temp poll every second, overtemp → fault */
		if ((g_state_ticks % SM_TIMING_TEMP_POLL) == 0) {
			ntcTempUpdate(SM_TIMING_TEMP_POLL);
			if (ntcTempOvertempFor() >= SM_TIMING_TEMP_FAULT_DELAY) {
				setError(STATEMACHINE_ERR_CHARGING_FAIL);
				return;
			}
			int16_t hi = ntcTempGetMax();
			if (hi > 0)  fanSet(true, fanDutyFromTemp(hi));
		}

		panelLedsUpdate();
	}
}

static void stateRunShutdown(void)
{
	if (g_state_ticks < SM_TIMING_RELAY_STEP) {
		bspDoutSetStatus(DOUT_K4_DRV, false);
	} else if (g_state_ticks < SM_TIMING_RELAY_STEP * 2) {
		bspDoutSetStatus(DOUT_K3_1_DRV, false);
		bspDoutSetStatus(DOUT_K3_2_DRV, false);
	} else if (g_state_ticks < SM_TIMING_RELAY_STEP * 3) {
		bspDoutSetMask(RELAY_MASK_K5_TO_K12, false);
		bspAoutSetState(AOUT_PWR_ON_OFF, false);
		bspDoutSetStatus(DOUT_TROLLEY_ENABLE_DRV, false);
		fanSet(false, FAN_DUTY_OFF);
	} else if (g_state_ticks < SM_TIMING_RELAY_STEP * 4) {
		panelLedsOff();
		bspLedSwitchOff(0);
		bspLedSwitchOff(1);
	} else {
		transitionTo(STATEMACHINE_STATE_OFF);
	}
}

static void stateRunFault(void)
{
	/* Error LED blink */
	if ((g_state_ticks % SM_TIMING_LED_BLINK_FAULT) < (SM_TIMING_LED_BLINK_FAULT / 2)) {
		bspLedSwitchOn(1);
	} else {
		bspLedSwitchOff(1);
	}

	/* Reset (external button 1s / on_off 5s) handled centrally in
	 * stateMachineTick(); it clears g_error and enters RESET. */

	/* Auto-recovery: only for transient mains loss when mains returns.
	 * Persistent faults (over-temp, switch-on failure, sensor fault) are
	 * not auto-recovered — they require an external reset, avoiding the
	 * fault→recover→re-fault oscillation. */
	if (g_state_ticks > SM_TIMING_FAULT_RECOVER) {
		if (g_faults & (uint32_t)STATEMACHINE_ERR_MAINS_LOSS) {
			if (checkMainsPresent()) {
				g_faults &= ~(uint32_t)STATEMACHINE_ERR_MAINS_LOSS;
				g_error = STATEMACHINE_ERR_RESET_RECOVERY;
				transitionTo(STATEMACHINE_STATE_RESET);
			}
		}
		/* else: persistent fault — wait for external reset only */
	}
}

static void stateRunReset(void)
{
	if (g_state_ticks < SM_TIMING_RESET_HOLD) {
		return;
	}

	g_faults = 0;
	g_error = STATEMACHINE_ERR_NONE;
	transitionTo(STATEMACHINE_STATE_INIT);
}

static void stateRunOff(void)
{
	/* Power-on (on_off rising 0.55s + mains OK) handled centrally in
	 * stateMachineTick(); nothing to do here while off. */
}

/* ==================== Input helpers ==================== */

static stateMachineConfig_t readConfigSwitches(void)
{
	if (bspDinGet(DIN_S1_SYSTEM_CONFIG)) {
		return STATEMACHINE_CFG_S1;
	} else if (bspDinGet(DIN_S2_SYSTEM_CONFIG)) {
		return STATEMACHINE_CFG_S2;
	} else if (bspDinGet(DIN_SOLO_SYSTEM_CONFIG)) {
		return STATEMACHINE_CFG_SOLO;
	}
	return STATEMACHINE_CFG_S1;
}

static bool checkMainsPresent(void)
{
	bool trolley = bspDinGet(DIN_TROLLEY_CONNECTED)
	            || bspDinGet(DIN_TROLLEY_CONNECTED_J);
	bool me_error = !bspDinGet(DIN_ME_BOX_ERROR);   /* PH6: high=normal -> low=fault */

	return trolley && !me_error;
}
