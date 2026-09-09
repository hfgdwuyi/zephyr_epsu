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
 *   sensor          — NTC temperature + voltage scaling (ADC interpretation)
 *   bsp_pwm         — fan PWM control
 *   bsp_aout        — pwr_on_off DAC output
 */

/* Zephyr */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* BSP */
#include "bsp_aout.h"
#include "bsp_dio.h"
#include "bsp_led.h"
#include "bsp_pwm.h"

/* Application */
#include "sensor.h"
#include "state_machine.h"
#include "uart_cmd.h"

/* ==================== Timing constants (ms) ==================== */

typedef enum {
	SM_TIMING_STARTUP_DELAY      = 100,
	SM_TIMING_K3_WAIT            = 200,
	SM_TIMING_K3_TIMEOUT         = 1000,   /* pwr_on_off feedback absent → K3 timeout */
	SM_TIMING_K3_STABLE          = 500,
	SM_TIMING_SWITCHON_TIMEOUT   = 2000,   /* grid relay feedback absent → switchOn fail */
	SM_TIMING_RELAY_STEP         = 50,
	SM_TIMING_S2_STABLE          = 50,
	SM_TIMING_CHARGING_STABLE    = 100,
	SM_TIMING_FAULT_RECOVER      = 3000,
	SM_TIMING_RESET_HOLD         = 2000,
	SM_TIMING_CHECKS_PERIOD      = 50,
	SM_TIMING_CONFIG_DEBOUNCE    = 20,
	SM_TIMING_LED_BLINK_FAULT    = 500,
	SM_TIMING_LED_BLINK_NORMAL   = 1000,
	SM_TIMING_FAN_SPIN_UP        = 100,
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
 * index ranges) stay correct even if unrelated pins sit in between.
 * New relay set (per pin_config.xlsx): K2..K13 + trolley_enable. */

/* K3 closes on switch-on; the rest of the main group closes on NORMAL_OP */
#define RELAY_MASK_K3        (BIT64(DOUT_K3_DRV))
#define RELAY_MASK_K2_TO_K13 (BIT64(DOUT_K2_DRV)   | RELAY_MASK_K3      | \
                              BIT64(DOUT_K4_DRV)   | BIT64(DOUT_K5_DRV)   | \
                              BIT64(DOUT_K6_DRV)   | BIT64(DOUT_K7_DRV)   | \
                              BIT64(DOUT_K8_1_EN)  | BIT64(DOUT_K8_2_EN)  | \
                              BIT64(DOUT_K9_EN)    | BIT64(DOUT_K10_EN)   | \
                              BIT64(DOUT_K11_EN)   | BIT64(DOUT_K12_EN)   | \
                              BIT64(DOUT_K13_EN))
#define RELAY_MASK_K5_TO_K13 (BIT64(DOUT_K5_DRV)   | BIT64(DOUT_K6_DRV)   | \
                              BIT64(DOUT_K7_DRV)   | BIT64(DOUT_K8_1_EN)  | \
                              BIT64(DOUT_K8_2_EN)  | BIT64(DOUT_K9_EN)    | \
                              BIT64(DOUT_K10_EN)   | BIT64(DOUT_K11_EN)   | \
                              BIT64(DOUT_K12_EN)   | BIT64(DOUT_K13_EN))
#define RELAY_MASK_ALL       RELAY_MASK_K2_TO_K13

#define TROLLEY_EN_MASK      BIT64(DOUT_TROLLEY_ENABLE_DRV)

/* 面板指示灯 mask（不含 PC8-10：那三颗是红/黄/绿状态灯，
 * 由 led_indicator 单独管理，防止状态机清灯逻辑误清心跳绿/告警红黄） */
#define PANEL_LED_MASK       (BIT64(DOUT_LED_PWR_24_ON)        | \
                              BIT64(DOUT_LED_CP_224V_ON)       | \
                              BIT64(DOUT_LED_GRID_PWR_IN)      | \
                              BIT64(DOUT_LED_UPS_IN)           | \
                              BIT64(DOUT_LED_SYSTEM_ON)        | \
                              BIT64(DOUT_LED_S2_SOLO_SYS)      | \
                              BIT64(DOUT_LED_TROLLEY_CONNECTED)| \
                              BIT64(DOUT_LED_IS_PC_ON)         | \
                              BIT64(DOUT_LED_S2_SYS_ON)        | \
                              BIT64(DOUT_LED_APP_HOST_ON))

/* ==================== Internal state ==================== */

/* volatile on cross-thread-visible state: written by sm_thread, read by
 * status_work / external getters. Prevents compiler caching stale values. */
static volatile stateMachineState_t  g_state         = STATEMACHINE_STATE_INIT;
static stateMachineConfig_t g_config        = STATEMACHINE_CFG_S1;
static volatile stateMachineError_t  g_error         = STATEMACHINE_ERR_NONE;
static volatile uint32_t     g_faults        = 0;
static int64_t     g_entry_ms      = 0;   /* wall-clock ms at state entry (k_uptime_get) */
static bool         g_stateEntered = false;
static bool         g_fan_ramped    = false;   /* one-shot fan spin-up ramp in NORMAL_OP */

/* DIN bitmap snapshot, captured once per tick at the top of stateMachineTick().
 * Every DIN read in this file tests against this snapshot so all reads within
 * one tick observe the same input state — mirroring the DOUT side's single
 * bitmap write (bspDoutSetBitmap). Refreshed again in stateMachineInit(). */
static uint32_t     s_din           = 0;

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

/* Wall-clock ms elapsed since the current state was entered. Anchoring all
 * SM_TIMING_* thresholds to k_uptime_get() (rather than a tick counter) keeps
 * them correct regardless of scheduling jitter or a long-running tick. */
static uint32_t elapsedInState(void)
{
	return (uint32_t)(k_uptime_get() - (int64_t)g_entry_ms);
}

/* ==================== Public API ==================== */

void stateMachineInit(void)
{
	g_state          = STATEMACHINE_STATE_INIT;
	g_error          = STATEMACHINE_ERR_NONE;
	g_faults         = 0;
	g_entry_ms       = k_uptime_get();
	g_stateEntered   = false;
	g_fan_ramped     = false;
	s_din            = bspDinGetBitmap();   /* snapshot before first config read */
	g_config         = readConfigSwitches();

	sensorTempInit();

	/* Start disarmed so a static power-on level cannot fire. */
	dinHoldInit(&s_onoff_rising);
	dinHoldInit(&s_onoff_falling);
	dinHoldInit(&s_reset_rising);

	printk("STATEMACHINE: init cfg=%d\n", (int)g_config);
}

void stateMachineTick(void)
{
	s_din = bspDinGetBitmap();   /* one input snapshot for the entire tick */

	if (!g_stateEntered) {
		stateEnter(g_state);
		g_stateEntered = true;
	}

	/* DIN hold detection: on_off toggle/reset, external reset.
	 * Runs every tick; actions depend on the current state. */
	bool onoff = (s_din & BIT(DIN_SYSTEM_ON_OFF)) != 0U;

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
		    dinHoldTick(&s_reset_rising, (s_din & BIT(DIN_SYSTEM_RESET)) != 0U, true, SM_TIMING_RESET_MS)) {
			/* on_off 1->0 held 5s, or external reset 0->1 held 1s */
			g_error = STATEMACHINE_ERR_NONE;
			stateMachineRequestReset();
		}
	}

	switch (g_state) {
	case STATEMACHINE_STATE_INIT:          stateRunInit();          break;
	case STATEMACHINE_STATE_SYS_ON:        stateRunSysOn();         break;
	case STATEMACHINE_STATE_PILOT_CONTACT: stateRunPilotContact();  break;
	case STATEMACHINE_STATE_SWITCH_ON:     stateRunSwitchOn();      break;
	case STATEMACHINE_STATE_NORMAL_OP:     stateRunNormalOp();      break;
	case STATEMACHINE_STATE_S2_MODE:       stateRunS2Mode();        break;
	case STATEMACHINE_STATE_CHARGING:      stateRunCharging();      break;
	case STATEMACHINE_STATE_SHUTDOWN:      stateRunShutdown();      break;
	case STATEMACHINE_STATE_FAULT:         stateRunFault();         break;
	case STATEMACHINE_STATE_RESET:         stateRunReset();         break;
	case STATEMACHINE_STATE_OFF:           stateRunOff();           break;
	default:
		g_state = STATEMACHINE_STATE_FAULT;
		g_stateEntered = false;
		g_entry_ms = k_uptime_get();
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
		if (!uartCmdDfuActive()) {
			printk("STATEMACHINE: shutdown requested\n");
		}
		g_state = STATEMACHINE_STATE_SHUTDOWN;
		g_stateEntered = false;
		g_entry_ms = k_uptime_get();
	}
}

void stateMachineRequestReset(void)
{
	if (!uartCmdDfuActive()) {
		printk("STATEMACHINE: reset requested\n");
	}
	g_state = STATEMACHINE_STATE_RESET;
	g_stateEntered = false;
	g_entry_ms = k_uptime_get();
}

void stateMachineRequestCharging(void)
{
	if (g_state == STATEMACHINE_STATE_NORMAL_OP) {
		if (!uartCmdDfuActive()) {
			printk("STATEMACHINE: charging requested\n");
		}
		g_state = STATEMACHINE_STATE_CHARGING;
		g_stateEntered = false;
		g_entry_ms = k_uptime_get();
	}
}

/* ==================== Helpers ==================== */

static void setError(stateMachineError_t err)
{
	g_error = err;
	g_faults |= (uint32_t)err;
	g_state = STATEMACHINE_STATE_FAULT;
	g_stateEntered = false;
	g_entry_ms = k_uptime_get();

	/* 故障日志：打印错误码、名称、当前状态与累计故障位。
	 * DFU 升级期间抑制，避免与串口 ACK 抢 USART1。 */
	if (!uartCmdDfuActive()) {
		printk("STATEMACHINE: FAULT err=0x%02X (%s) state=%d faults=0x%02X\n",
		       (unsigned)err, stateMachineGetErrorStr(err),
		       (int)g_state, (unsigned)g_faults);
	}
}

static void transitionTo(stateMachineState_t s)
{
	g_state = s;
	g_stateEntered = false;
	g_entry_ms = k_uptime_get();
}

/* ==================== Panel LED control ==================== */

static void panelLedsOff(void)
{
	bspDoutSetBitmap(PANEL_LED_MASK, false);
}

static void panelLedsUpdate(void)
{

	if (g_state == STATEMACHINE_STATE_INIT) {
		panelLedsOff();
		return;
	}

	bool grid_ok  = (s_din & BIT(DIN_GRID_MAIN_RELAY_STATUS)) != 0U;  /* PH5: high=valid */
	bool me_err   = (s_din & BIT(DIN_ME_BOX_ERROR)) == 0U;            /* PH6: high=normal -> low=fault */
	bool trolley  = (s_din & BIT(DIN_TROLLEY_CONNECTED)) != 0U;       /* PJ5: high=connected */
	bool is_pc    = (s_din & BIT(DIN_IS_PC_ON)) != 0U;
	bool app_host = (s_din & BIT(DIN_APP_HOST_ON)) != 0U;
	bool s2       = (s_din & BIT(DIN_S2_SYSTEM_CONFIG)) != 0U;
	bool solo     = (s_din & BIT(DIN_SOLO_SYSTEM_CONFIG)) != 0U;

	/* 24V power LED follows the DIN feedback contact (PC6) */
	bool pwr_24 = (s_din & BIT(DIN_LED_PWR_24_ON)) != 0U;

	/* led_system_on blinks in normal/charging, solid in others */
	bool sys_led;
	if (g_state == STATEMACHINE_STATE_NORMAL_OP || g_state == STATEMACHINE_STATE_CHARGING) {
		sys_led = ((elapsedInState() % SM_TIMING_LED_BLINK_NORMAL) < (SM_TIMING_LED_BLINK_NORMAL / 2));
	} else {
		sys_led = true;
	}

	/* Derive the full panel-LED "on" set, then two-phase commit:
	 * clear all panel LEDs, set the ones that should be lit. */
	uint64_t on =
		(pwr_24             ? BIT64(DOUT_LED_PWR_24_ON)    : 0U) |
		(s2                 ? BIT64(DOUT_LED_S2_SYS_ON)    : 0U) |
		(s2 || solo         ? BIT64(DOUT_LED_S2_SOLO_SYS)  : 0U) |
		(grid_ok            ? BIT64(DOUT_LED_GRID_PWR_IN)  : 0U) |
		(!grid_ok           ? BIT64(DOUT_LED_UPS_IN)       : 0U) |
		(trolley            ? BIT64(DOUT_LED_TROLLEY_CONNECTED) : 0U) |
		(is_pc              ? BIT64(DOUT_LED_IS_PC_ON)     : 0U) |
		(app_host           ? BIT64(DOUT_LED_APP_HOST_ON)  : 0U) |
		(sys_led && !me_err ? BIT64(DOUT_LED_SYSTEM_ON)    : 0U);

	bspDoutSetBitmap(PANEL_LED_MASK, false);   /* clear all panel LEDs */
	bspDoutSetBitmap(on, true);                /* set the ones that should be lit */
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
	if (temp_max >= SENSOR_TEMP_THRESH_FAULT)  return FAN_DUTY_MAX;
	if (temp_max >= SENSOR_TEMP_THRESH_WARN)   return FAN_DUTY_WARN;
	if (temp_max >= SENSOR_TEMP_THRESH_FAN_MAX) return FAN_DUTY_HIGH;
	if (temp_max >= SENSOR_TEMP_THRESH_FAN_MID) return FAN_DUTY_MID;
	return FAN_DUTY_MIN;
}

static void relaysAllOff(void)
{
	bspDoutSetBitmap(RELAY_MASK_ALL, false);
}

/* ==================== State entry ==================== */

static void stateEnter(stateMachineState_t s)
{
	if (!uartCmdDfuActive()) {
		printk("STATEMACHINE: -> state %d (t=%ums)\n", (int)s,
		       (unsigned)elapsedInState());
	}

	switch (s) {

	case STATEMACHINE_STATE_INIT:
		bspDoutSetBitmap(TROLLEY_EN_MASK, false);
		bspAoutSetState(AOUT_PWR_ON_OFF, false);
		relaysAllOff();
		panelLedsOff();
		fanSet(false, FAN_DUTY_OFF);
		bspLedSwitchOff(1);
		break;

	case STATEMACHINE_STATE_SYS_ON:
		bspDoutSetBitmap(TROLLEY_EN_MASK, true);
		panelLedsUpdate();
		break;

	case STATEMACHINE_STATE_PILOT_CONTACT:
		bspAoutSetState(AOUT_PWR_ON_OFF, true);   /* pwr_on_off: 0-1.5V @ 0.25Hz square wave */
		break;

	case STATEMACHINE_STATE_SWITCH_ON:
		bspDoutSetBitmap(RELAY_MASK_K3, true);
		break;

	case STATEMACHINE_STATE_NORMAL_OP:
		bspDoutSetBitmap(RELAY_MASK_K2_TO_K13, true);
		panelLedsUpdate();
		fanSet(true, FAN_DUTY_NORMAL);   /* 50% initial, ramp up over time */
		g_fan_ramped = false;
		break;

	case STATEMACHINE_STATE_S2_MODE:
		bspDoutSetBitmap(RELAY_MASK_K5_TO_K13, false);
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
		bspLedSwitchOn(1);
		break;

	case STATEMACHINE_STATE_OFF:
		bspDoutSetBitmap(TROLLEY_EN_MASK, false);
		bspAoutSetState(AOUT_PWR_ON_OFF, false);
		bspDoutSetBitmap(RELAY_MASK_K3 | BIT64(DOUT_K4_DRV), false);
		panelLedsOff();
		fanSet(false, FAN_DUTY_OFF);
		bspLedSwitchOff(1);
		break;

	default:
		break;
	}
}

/* ==================== State runners ==================== */

static void stateRunInit(void)
{
	uint32_t el = elapsedInState();

	if (el < SM_TIMING_STARTUP_DELAY) {
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
	uint32_t el = elapsedInState();

	if (el < SM_TIMING_CONFIG_DEBOUNCE) {
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
	uint32_t el = elapsedInState();

	if (el < SM_TIMING_K3_WAIT) {
		return;
	}

	/* Verify pwr_on_off feedback: DIN_LED_PWR_24_ON (PC6) */
	if ((s_din & BIT(DIN_LED_PWR_24_ON)) == 0U) {
		if (el > SM_TIMING_K3_TIMEOUT) {
			setError(STATEMACHINE_ERR_K3_TIMEOUT);
		}
		return;
	}

	transitionTo(STATEMACHINE_STATE_SWITCH_ON);
}

static void stateRunSwitchOn(void)
{
	uint32_t el = elapsedInState();

	if (el < SM_TIMING_K3_STABLE) {
		return;
	}

	if ((s_din & BIT(DIN_GRID_MAIN_RELAY_STATUS)) == 0U) {
		if (el > SM_TIMING_SWITCHON_TIMEOUT) {
			setError(STATEMACHINE_ERR_SWITCHON_FAIL);
		}
		return;
	}

	transitionTo(STATEMACHINE_STATE_NORMAL_OP);
}

static void stateRunNormalOp(void)
{
	uint32_t el = elapsedInState();

	if ((el % SM_TIMING_CONFIG_DEBOUNCE) == 0) {
		stateMachineConfig_t cfg = readConfigSwitches();
		if (cfg != g_config) {
			g_config = cfg;
			if (cfg == STATEMACHINE_CFG_S2) {
				transitionTo(STATEMACHINE_STATE_S2_MODE);
				return;
			}
		}
	}

	if ((el % SM_TIMING_CHECKS_PERIOD) == 0) {
		if (!checkMainsPresent()) {
			setError(STATEMACHINE_ERR_MAINS_LOSS);
			return;
		}
		/* on_off toggle is handled centrally in stateMachineTick() */
	}

	/* Periodic panel LED refresh + temperature checks.
	 * Temperatures are sampled by the sensor thread; here we only query. */
	if ((el % SM_TIMING_CHECKS_PERIOD) == 0) {
		panelLedsUpdate();

		/* Sensor fault: report immediately rather than treating as cold,
		 * which would silently disable over-temp protection. */
		if (sensorTempSensorFault()) {
			printk("STATEMACHINE: NTC sensor fault!\n");
			setError(STATEMACHINE_ERR_INIT_FAIL);
			return;
		}

		if (sensorTempOvertempFor() >= SM_TIMING_TEMP_FAULT_DELAY) {
			printk("STATEMACHINE: over-temp fault! Tmax=%d.%d\n",
			       sensorTempGetMax() / 10, sensorTempGetMax() % 10);
			setError(STATEMACHINE_ERR_INIT_FAIL);
			return;
		}

		/* Adjust fan speed based on temperature */
		int16_t hi = sensorTempGetMax();
		if (hi > 0 && el > SM_TIMING_FAN_SPIN_UP) {
			fanSet(true, fanDutyFromTemp(hi));
		}
	}

	/* Ramp fan to full speed after spin-up (only if no temp data yet).
	 * Range check (>=) instead of == so a missed tick cannot skip it. */
	if (!g_fan_ramped && el >= SM_TIMING_FAN_SPIN_UP && sensorTempGetMax() <= 0) {
		fanSet(true, FAN_DUTY_WARN);
		g_fan_ramped = true;
	}
}

static void stateRunS2Mode(void)
{
	uint32_t el = elapsedInState();

	if (el < SM_TIMING_S2_STABLE) {
		return;
	}

	if ((el % SM_TIMING_CHECKS_PERIOD) == 0) {
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
	uint32_t el = elapsedInState();

	if (el < SM_TIMING_CHARGING_STABLE) {
		return;
	}

	if ((el % SM_TIMING_CHECKS_PERIOD) == 0) {
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

		/* Over-temp duration is accumulated by the sensor thread */
		if (sensorTempOvertempFor() >= SM_TIMING_TEMP_FAULT_DELAY) {
			setError(STATEMACHINE_ERR_CHARGING_FAIL);
			return;
		}
		int16_t hi = sensorTempGetMax();
		if (hi > 0)  fanSet(true, fanDutyFromTemp(hi));

		panelLedsUpdate();
	}
}

static void stateRunShutdown(void)
{
	uint32_t el = elapsedInState();

	if (el < SM_TIMING_RELAY_STEP) {
		bspDoutSetBitmap(BIT64(DOUT_K4_DRV), false);
	} else if (el < SM_TIMING_RELAY_STEP * 2) {
		bspDoutSetBitmap(BIT64(DOUT_K3_DRV), false);
	} else if (el < SM_TIMING_RELAY_STEP * 3) {
		bspDoutSetBitmap(RELAY_MASK_K5_TO_K13, false);
		bspAoutSetState(AOUT_PWR_ON_OFF, false);
		bspDoutSetBitmap(TROLLEY_EN_MASK, false);
		fanSet(false, FAN_DUTY_OFF);
	} else if (el < SM_TIMING_RELAY_STEP * 4) {
		panelLedsOff();
		bspLedSwitchOff(1);
	} else {
		transitionTo(STATEMACHINE_STATE_OFF);
	}
}

static void stateRunFault(void)
{
	uint32_t el = elapsedInState();

	/* Error LED blink */
	if ((el % SM_TIMING_LED_BLINK_FAULT) < (SM_TIMING_LED_BLINK_FAULT / 2)) {
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
	if (el > SM_TIMING_FAULT_RECOVER) {
		if (g_faults & (uint32_t)STATEMACHINE_ERR_MAINS_LOSS) {
			if (checkMainsPresent()) {
				if (!uartCmdDfuActive()) {
					printk("STATEMACHINE: mains restored, auto-recover\n");
				}
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
	if (elapsedInState() < SM_TIMING_RESET_HOLD) {
		return;
	}

	if (!uartCmdDfuActive()) {
		printk("STATEMACHINE: reset complete, faults cleared (was 0x%02X)\n",
		       (unsigned)g_faults);
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
	if ((s_din & BIT(DIN_S1_SYSTEM_CONFIG)) != 0U) {
		return STATEMACHINE_CFG_S1;
	} else if ((s_din & BIT(DIN_S2_SYSTEM_CONFIG)) != 0U) {
		return STATEMACHINE_CFG_S2;
	} else if ((s_din & BIT(DIN_SOLO_SYSTEM_CONFIG)) != 0U) {
		return STATEMACHINE_CFG_SOLO;
	}
	return STATEMACHINE_CFG_S1;
}

static bool checkMainsPresent(void)
{
	bool trolley = (s_din & BIT(DIN_TROLLEY_CONNECTED)) != 0U;
	bool me_error = (s_din & BIT(DIN_ME_BOX_ERROR)) == 0U;   /* PH6: high=normal -> low=fault */

	return trolley && !me_error;
}
