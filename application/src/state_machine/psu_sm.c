/*
 * psu_sm.c
 *
 * ePSU Power Supply State Machine — cios-zhong implementation.
 *
 * State transitions and relay/LED/fan sequencing per ePSU timing diagram.
 * Tick rate: 1 ms.
 *
 * Layer split:
 *   psu_sm   — relay driver control (K3-K12, pwr_on_off, trolley_enable)
 *   psu_sm   — panel LED indicators (reflect system state)
 *   hal_pwm  — fan PWM control
 */

#include "psu_sm.h"

#include <string.h>
#include <zephyr/sys/printk.h>

#include "bsp_dio.h"
#include "bsp_pwm.h"
#include "bsp_led.h"

#include "ntc_sensor.h"

/* ==================== Timing constants (ms) ==================== */

#define T_STARTUP_DELAY      100
#define T_K3_WAIT            200
#define T_K3_STABLE          500
#define T_RELAY_STEP          50
#define T_FAULT_RECOVER      3000
#define T_RESET_HOLD         2000
#define T_CHECKS_PERIOD        50
#define T_CONFIG_DEBOUNCE      20
#define T_LED_BLINK_FAULT     500
#define T_LED_BLINK_NORMAL    1000
#define T_FAN_SPIN_UP         100
#define T_TEMP_POLL           1000     /* poll NTC every 1000 ms */
#define T_TEMP_FAULT_DELAY    5000     /* 5s over-temp persist before fault */

/* ==================== Internal state ==================== */

static psu_state_t  g_state         = PSU_STATE_INIT;
static psu_config_t g_config        = PSU_CFG_S1;
static psu_error_t  g_error         = PSU_ERR_NONE;
static uint32_t     g_faults        = 0;
static uint32_t     g_state_ticks   = 0;
static bool         g_stateEntered = false;
static uint32_t     g_temp_ticks    = 0;      /* consecutive over-temp ms  */
static int16_t      g_temp1         = 0;      /* temp sensor 1 (×10°C)     */
static int16_t      g_temp2         = 0;      /* temp sensor 2 (×10°C)     */

/* ==================== Forward declarations ==================== */

static void stateEnter(psu_state_t s);
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

static psu_config_t readConfigSwitches(void);
static bool checkMainsPresent(void);
static void panelLedsOff(void);
static void panelLedsUpdate(psu_state_t s);
static void fanSet(bool on, uint32_t duty_percent);

/* ==================== Public API ==================== */

void psuSmInit(void)
{
	g_state          = PSU_STATE_INIT;
	g_error          = PSU_ERR_NONE;
	g_faults         = 0;
	g_state_ticks    = 0;
	g_stateEntered  = false;
	g_config         = readConfigSwitches();

	printk("PSU_SM: init cfg=%d\n", (int)g_config);
}

void psuSmTick(void)
{
	g_state_ticks++;

	if (!g_stateEntered) {
		stateEnter(g_state);
		g_stateEntered = true;
	}

	switch (g_state) {
	case PSU_STATE_INIT:          stateRunInit();          break;
	case PSU_STATE_SYS_ON:        stateRunSysOn();        break;
	case PSU_STATE_PILOT_CONTACT: stateRunPilotContact(); break;
	case PSU_STATE_SWITCH_ON:     stateRunSwitchOn();     break;
	case PSU_STATE_NORMAL_OP:     stateRunNormalOp();     break;
	case PSU_STATE_S2_MODE:       stateRunS2Mode();       break;
	case PSU_STATE_CHARGING:      stateRunCharging();      break;
	case PSU_STATE_SHUTDOWN:      stateRunShutdown();      break;
	case PSU_STATE_FAULT:         stateRunFault();         break;
	case PSU_STATE_RESET:         stateRunReset();         break;
	case PSU_STATE_OFF:           stateRunOff();           break;
	default:
		g_state = PSU_STATE_FAULT;
		break;
	}
}

psu_state_t psuSmGetState(void)           { return g_state; }
uint32_t   psuSmGetFaults(void)           { return g_faults; }
psu_error_t psuSmGetError(void)           { return g_error; }

const char *psuSmGetErrorStr(psu_error_t err)
{
	switch (err) {
	case PSU_ERR_NONE:            return "OK";
	case PSU_ERR_INIT_FAIL:       return "ESLR.35 init error";
	case PSU_ERR_K3_TIMEOUT:      return "ESTP3.36 K3 close timeout";
	case PSU_ERR_SWITCHON_FAIL:   return "ESIC1.37 switchOn fail";
	case PSU_ERR_MAINS_LOSS:      return "ESTP1.38 mains PWR failure";
	case PSU_ERR_RESET_RECOVERY:  return "EAKO3.39 reset after error";
	case PSU_ERR_CHARGING_FAIL:   return "ESIC.40 charging control err";
	default:                      return "unknown";
	}
}

void psuSmRequestShutdown(void)
{
	if (g_state == PSU_STATE_NORMAL_OP ||
	    g_state == PSU_STATE_S2_MODE ||
	    g_state == PSU_STATE_CHARGING) {
		printk("PSU_SM: shutdown requested\n");
		g_state = PSU_STATE_SHUTDOWN;
		g_stateEntered = false;
		g_state_ticks = 0;
	}
}

void psuSmRequestReset(void)
{
	printk("PSU_SM: reset requested\n");
	g_state = PSU_STATE_RESET;
	g_stateEntered = false;
	g_state_ticks = 0;
}

void psuSmRequestCharging(void)
{
	if (g_state == PSU_STATE_NORMAL_OP) {
		printk("PSU_SM: charging requested\n");
		g_state = PSU_STATE_CHARGING;
		g_stateEntered = false;
		g_state_ticks = 0;
	}
}

/* ==================== Helpers ==================== */

static void setError(psu_error_t err)
{
	g_error = err;
	g_faults |= (uint32_t)err;
	g_state = PSU_STATE_FAULT;
	g_stateEntered = false;
	g_state_ticks = 0;
}

static void transitionTo(psu_state_t s)
{
	g_state = s;
	g_stateEntered = false;
	g_state_ticks = 0;
}

/* ==================== Panel LED control ==================== */

static void panelLedsOff(void)
{
	bspDoutSet(DOUT_LED_S1_SYS_ON,         false);
	bspDoutSet(DOUT_LED_S2_SYS_ON,         false);
	bspDoutSet(DOUT_DBG_LED0,              false);
	bspDoutSet(DOUT_DBG_LED1,              false);
	bspDoutSet(DOUT_DBG_LED2,              false);
	bspDoutSet(DOUT_LED_PAC230V_ON,        false);
	bspDoutSet(DOUT_LED_GRID_PWR_IN,       false);
	bspDoutSet(DOUT_LED_UPS_IN,            false);
	bspDoutSet(DOUT_LED_SYSTEM_ON,         false);
	bspDoutSet(DOUT_LED_S2_SOLO_SYS,       false);
	bspDoutSet(DOUT_LED_TROLLEY_CONNECTED, false);
	bspDoutSet(DOUT_LED_IS_PC_ON,          false);
	bspDoutSet(DOUT_LED_APPHOST_ON,        false);
}

static void panelLedsUpdate(psu_state_t s)
{
	(void)s;

	if (g_state == PSU_STATE_INIT) {
		panelLedsOff();
		return;
	}

	bool grid_ok  = bspDinGet(DIN_GRID_MAIN_RELAY_STATUS);
	bool me_err   = bspDinGet(DIN_ME_BOX_ERROR);
	bool trolley  = bspDinGet(DIN_TROLLEY_CONNECTED);
	bool is_pc    = bspDinGet(DIN_IS_PC_ON);
	bool app_host = bspDinGet(DIN_APP_HOST_ON);
	bool s1       = bspDinGet(DIN_S1_SYSTEM_CONFIG);
	bool s2       = bspDinGet(DIN_S2_SYSTEM_CONFIG);
	bool solo     = bspDinGet(DIN_SOLO_SYSTEM_CONFIG);

	bspDoutSet(DOUT_LED_S1_SYS_ON,         s1);
	bspDoutSet(DOUT_LED_S2_SYS_ON,         s2);
	bspDoutSet(DOUT_LED_S2_SOLO_SYS,       s2 || solo);
	bspDoutSet(DOUT_LED_PAC230V_ON,        grid_ok && !me_err);
	bspDoutSet(DOUT_LED_GRID_PWR_IN,       grid_ok);
	bspDoutSet(DOUT_LED_UPS_IN,            !grid_ok);
	bspDoutSet(DOUT_LED_TROLLEY_CONNECTED, trolley);
	bspDoutSet(DOUT_LED_IS_PC_ON,          is_pc);
	bspDoutSet(DOUT_LED_APPHOST_ON,        app_host);

	/* led_system_on blinks in normal/charging, solid in others */
	bool sys_led;
	if (g_state == PSU_STATE_NORMAL_OP || g_state == PSU_STATE_CHARGING) {
		sys_led = ((g_state_ticks % T_LED_BLINK_NORMAL) < (T_LED_BLINK_NORMAL / 2));
	} else {
		sys_led = true;
	}
	bspDoutSet(DOUT_LED_SYSTEM_ON, sys_led && !me_err);
}

/* ==================== Fan control (PWM) ==================== */

static void fanSet(bool on, uint32_t duty_percent)
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
static uint32_t fanDutyFromTemp(int16_t temp_max)
{
	if (temp_max >= NTC_TEMP_FAULT)  return 100;
	if (temp_max >= NTC_TEMP_WARN)   return  80;
	if (temp_max >= NTC_TEMP_FAN_MAX) return 60;
	if (temp_max >= NTC_TEMP_FAN_MID) return 40;
	return 20;
}

/* ==================== Temperature monitoring ==================== */

static void tempUpdate(void)
{
	g_temp1 = ntcReadTemp(AIN_TEMP1);
	g_temp2 = ntcReadTemp(AIN_TEMP2);
}

static bool tempOvertemp(void)
{
	int16_t hi = (g_temp1 > g_temp2) ? g_temp1 : g_temp2;
	return (hi > 0) && (hi >= NTC_TEMP_FAULT);
}

/* ==================== State entry ==================== */

static void stateEnter(psu_state_t s)
{
	printk("PSU_SM: -> state %d (t=%ums)\n", (int)s, (unsigned)g_state_ticks);

	switch (s) {

	case PSU_STATE_INIT:
		bspDoutSet(DOUT_TROLLEY_ENABLE_DRV, false);
		bspDoutSet(DOUT_PWR_ON_OFF,         false);
		bspDoutSet(DOUT_K3_1_DRV,           false);
		bspDoutSet(DOUT_K3_2_DRV,           false);
		bspDoutSet(DOUT_K4_DRV,             false);
		bspDoutSet(DOUT_K5_DRV,             false);
		bspDoutSet(DOUT_K6_DRV,             false);
		bspDoutSet(DOUT_K8_1_DRV,           false);
		bspDoutSet(DOUT_K8_2_DRV,           false);
		bspDoutSet(DOUT_K9_DRV,             false);
		bspDoutSet(DOUT_K10_DRV,            false);
		bspDoutSet(DOUT_K11_DRV,            false);
		bspDoutSet(DOUT_K12_DRV,            false);
		panelLedsOff();
		fanSet(false, 0);
		ledSwitchOff(0);
		ledSwitchOff(1);
		break;

	case PSU_STATE_SYS_ON:
		bspDoutSet(DOUT_TROLLEY_ENABLE_DRV, true);
		panelLedsUpdate(s);
		ledSwitchOn(0);
		break;

	case PSU_STATE_PILOT_CONTACT:
		bspDoutSet(DOUT_PWR_ON_OFF, true);
		break;

	case PSU_STATE_SWITCH_ON:
		bspDoutSet(DOUT_K3_1_DRV, true);
		bspDoutSet(DOUT_K3_2_DRV, true);
		break;

	case PSU_STATE_NORMAL_OP:
		bspDoutSet(DOUT_K4_DRV,   true);
		bspDoutSet(DOUT_K5_DRV,   true);
		bspDoutSet(DOUT_K6_DRV,   true);
		bspDoutSet(DOUT_K8_1_DRV, true);
		bspDoutSet(DOUT_K8_2_DRV, true);
		bspDoutSet(DOUT_K9_DRV,   true);
		bspDoutSet(DOUT_K10_DRV,  true);
		bspDoutSet(DOUT_K11_DRV,  true);
		bspDoutSet(DOUT_K12_DRV,  true);
		panelLedsUpdate(s);
		fanSet(true, 50);   /* 50% initial, ramp up over time */
		break;

	case PSU_STATE_S2_MODE:
		bspDoutSet(DOUT_K5_DRV,   false);
		bspDoutSet(DOUT_K6_DRV,   false);
		bspDoutSet(DOUT_K8_1_DRV, false);
		bspDoutSet(DOUT_K8_2_DRV, false);
		bspDoutSet(DOUT_K9_DRV,   false);
		bspDoutSet(DOUT_K10_DRV,  false);
		bspDoutSet(DOUT_K11_DRV,  false);
		bspDoutSet(DOUT_K12_DRV,  false);
		panelLedsUpdate(s);
		fanSet(true, 30);   /* low power, minimal cooling */
		break;

	case PSU_STATE_CHARGING:
		panelLedsUpdate(s);
		fanSet(true, 70);   /* charging needs extra cooling */
		break;

	case PSU_STATE_SHUTDOWN:
		bspDoutSet(DOUT_K4_DRV,   false);
		bspDoutSet(DOUT_K5_DRV,   false);
		bspDoutSet(DOUT_K6_DRV,   false);
		bspDoutSet(DOUT_K8_1_DRV, false);
		bspDoutSet(DOUT_K8_2_DRV, false);
		bspDoutSet(DOUT_K9_DRV,   false);
		bspDoutSet(DOUT_K10_DRV,  false);
		bspDoutSet(DOUT_K11_DRV,  false);
		bspDoutSet(DOUT_K12_DRV,  false);
		/* Fans stay on briefly for cool-down */
		break;

	case PSU_STATE_FAULT:
		bspDoutSet(DOUT_PWR_ON_OFF,         false);
		bspDoutSet(DOUT_K3_1_DRV,           false);
		bspDoutSet(DOUT_K3_2_DRV,           false);
		bspDoutSet(DOUT_K4_DRV,             false);
		bspDoutSet(DOUT_K5_DRV,  false); bspDoutSet(DOUT_K6_DRV,  false);
		bspDoutSet(DOUT_K8_1_DRV, false); bspDoutSet(DOUT_K8_2_DRV, false);
		bspDoutSet(DOUT_K9_DRV,  false); bspDoutSet(DOUT_K10_DRV, false);
		bspDoutSet(DOUT_K11_DRV, false); bspDoutSet(DOUT_K12_DRV, false);
		panelLedsOff();
		fanSet(false, 0);
		ledSwitchOff(0);
		ledSwitchOn(1);
		break;

	case PSU_STATE_OFF:
		bspDoutSet(DOUT_TROLLEY_ENABLE_DRV, false);
		bspDoutSet(DOUT_PWR_ON_OFF,         false);
		bspDoutSet(DOUT_K3_1_DRV,           false);
		bspDoutSet(DOUT_K3_2_DRV,           false);
		bspDoutSet(DOUT_K4_DRV,             false);
		panelLedsOff();
		fanSet(false, 0);
		ledSwitchOff(0);
		ledSwitchOff(1);
		break;

	default:
		break;
	}
}

/* ==================== State runners ==================== */

static void stateRunInit(void)
{
	if (g_state_ticks < T_STARTUP_DELAY) {
		return;
	}

	g_config = readConfigSwitches();

	if (!checkMainsPresent()) {
		return;
	}

	transitionTo(PSU_STATE_SYS_ON);
}

static void stateRunSysOn(void)
{
	if (g_state_ticks < T_CONFIG_DEBOUNCE) {
		return;
	}

	g_config = readConfigSwitches();

	if (!checkMainsPresent()) {
		setError(PSU_ERR_MAINS_LOSS);
		return;
	}

	transitionTo(PSU_STATE_PILOT_CONTACT);
}

static void stateRunPilotContact(void)
{
	if (g_state_ticks < T_K3_WAIT) {
		return;
	}

	/* Verify pwr_on_off feedback: DIN_LED_PWR_24_ON (PC6) */
	if (!bspDinGet(DIN_LED_PWR_24_ON)) {
		if (g_state_ticks > 1000) {
			setError(PSU_ERR_K3_TIMEOUT);
		}
		return;
	}

	transitionTo(PSU_STATE_SWITCH_ON);
}

static void stateRunSwitchOn(void)
{
	if (g_state_ticks < T_K3_STABLE) {
		return;
	}

	if (!bspDinGet(DIN_GRID_MAIN_RELAY_STATUS)) {
		if (g_state_ticks > 2000) {
			setError(PSU_ERR_SWITCHON_FAIL);
		}
		return;
	}

	transitionTo(PSU_STATE_NORMAL_OP);
}

static void stateRunNormalOp(void)
{
	if ((g_state_ticks % T_CONFIG_DEBOUNCE) == 0) {
		psu_config_t cfg = readConfigSwitches();
		if (cfg != g_config) {
			g_config = cfg;
			if (cfg == PSU_CFG_S2) {
				transitionTo(PSU_STATE_S2_MODE);
				return;
			}
		}
	}

	if ((g_state_ticks % T_CHECKS_PERIOD) == 0) {
		if (!checkMainsPresent()) {
			setError(PSU_ERR_MAINS_LOSS);
			return;
		}
		if (!bspDinGet(DIN_SYSTEM_ON_OFF)) {
			transitionTo(PSU_STATE_SHUTDOWN);
			return;
		}
	}

	/* Periodic panel LED refresh */
	if ((g_state_ticks % T_CHECKS_PERIOD) == 0) {
		panelLedsUpdate(PSU_STATE_NORMAL_OP);
	}

	/* Temperature polling */
	if ((g_state_ticks % T_TEMP_POLL) == 0) {
		tempUpdate();

		if (tempOvertemp()) {
			g_temp_ticks += T_TEMP_POLL;
		} else {
			g_temp_ticks = 0;
		}

		if (g_temp_ticks >= T_TEMP_FAULT_DELAY) {
			printk("PSU_SM: over-temp fault! T1=%d.%d T2=%d.%d\n",
			       g_temp1 / 10, g_temp1 % 10,
			       g_temp2 / 10, g_temp2 % 10);
			setError(PSU_ERR_INIT_FAIL);
			return;
		}

		/* Adjust fan speed based on temperature */
		int16_t hi = (g_temp1 > g_temp2) ? g_temp1 : g_temp2;
		if (hi > 0 && g_state_ticks > T_FAN_SPIN_UP) {
			fanSet(true, fanDutyFromTemp(hi));
		}
	}

	/* Ramp fan to full speed after spin-up (only if no temp data yet) */
	if (g_state_ticks == T_FAN_SPIN_UP && g_temp1 <= 0 && g_temp2 <= 0) {
		fanSet(true, 80);
	}
}

static void stateRunS2Mode(void)
{
	if (g_state_ticks < 50) {
		return;
	}

	if ((g_state_ticks % T_CHECKS_PERIOD) == 0) {
		psu_config_t cfg = readConfigSwitches();
		if (cfg != PSU_CFG_S2) {
			g_config = cfg;
			transitionTo(PSU_STATE_NORMAL_OP);
			return;
		}
		if (!checkMainsPresent()) {
			setError(PSU_ERR_MAINS_LOSS);
			return;
		}
		if (!bspDinGet(DIN_SYSTEM_ON_OFF)) {
			transitionTo(PSU_STATE_SHUTDOWN);
			return;
		}
		panelLedsUpdate(PSU_STATE_S2_MODE);
	}
}

static void stateRunCharging(void)
{
	if (g_state_ticks < 100) {
		return;
	}

	if ((g_state_ticks % T_CHECKS_PERIOD) == 0) {
		psu_config_t cfg = readConfigSwitches();
		if (cfg != g_config) {
			g_config = cfg;
			transitionTo(PSU_STATE_NORMAL_OP);
			return;
		}
		if (!checkMainsPresent()) {
			setError(PSU_ERR_CHARGING_FAIL);
			return;
		}
		if (!bspDinGet(DIN_SYSTEM_ON_OFF)) {
			transitionTo(PSU_STATE_SHUTDOWN);
			return;
		}

		/* Temp poll every second, overtemp → fault */
		if ((g_state_ticks % T_TEMP_POLL) == 0) {
			tempUpdate();
			if (tempOvertemp()) { g_temp_ticks += T_TEMP_POLL; }
			else                  { g_temp_ticks = 0; }
			if (g_temp_ticks >= T_TEMP_FAULT_DELAY) {
				setError(PSU_ERR_CHARGING_FAIL);
				return;
			}
			int16_t hi = (g_temp1 > g_temp2) ? g_temp1 : g_temp2;
			if (hi > 0)  fanSet(true, fanDutyFromTemp(hi));
		}

		panelLedsUpdate(PSU_STATE_CHARGING);
	}
}

static void stateRunShutdown(void)
{
	if (g_state_ticks < T_RELAY_STEP) {
		bspDoutSet(DOUT_K4_DRV, false);
	} else if (g_state_ticks < T_RELAY_STEP * 2) {
		bspDoutSet(DOUT_K3_1_DRV, false);
		bspDoutSet(DOUT_K3_2_DRV, false);
	} else if (g_state_ticks < T_RELAY_STEP * 3) {
		bspDoutSet(DOUT_PWR_ON_OFF,         false);
		bspDoutSet(DOUT_TROLLEY_ENABLE_DRV, false);
		fanSet(false, 0);
	} else if (g_state_ticks < T_RELAY_STEP * 4) {
		panelLedsOff();
		ledSwitchOff(0);
		ledSwitchOff(1);
	} else {
		transitionTo(PSU_STATE_OFF);
	}
}

static void stateRunFault(void)
{
	/* Error LED blink */
	if ((g_state_ticks % T_LED_BLINK_FAULT) < (T_LED_BLINK_FAULT / 2)) {
		ledSwitchOn(1);
	} else {
		ledSwitchOff(1);
	}

	/* Manual reset: DIN_SYSTEM_RESET (PJ1) */
	if (bspDinGet(DIN_SYSTEM_RESET)) {
		g_error = PSU_ERR_NONE;
		transitionTo(PSU_STATE_RESET);
		return;
	}

	/* Auto-recovery timeout */
	if (g_state_ticks > T_FAULT_RECOVER) {
		if (g_faults & (uint32_t)PSU_ERR_MAINS_LOSS) {
			if (checkMainsPresent()) {
				g_faults &= ~(uint32_t)PSU_ERR_MAINS_LOSS;
				g_error = PSU_ERR_RESET_RECOVERY;
				transitionTo(PSU_STATE_RESET);
			}
		} else {
			g_faults = 0;
			g_error = PSU_ERR_RESET_RECOVERY;
			transitionTo(PSU_STATE_RESET);
		}
	}
}

static void stateRunReset(void)
{
	if (g_state_ticks < T_RESET_HOLD) {
		return;
	}

	g_faults = 0;
	g_error = PSU_ERR_NONE;
	transitionTo(PSU_STATE_INIT);
}

static void stateRunOff(void)
{
	if (bspDinGet(DIN_SYSTEM_ON_OFF) && checkMainsPresent()) {
		transitionTo(PSU_STATE_INIT);
	}
}

/* ==================== Input helpers ==================== */

static psu_config_t readConfigSwitches(void)
{
	if (bspDinGet(DIN_S1_SYSTEM_CONFIG)) {
		return PSU_CFG_S1;
	} else if (bspDinGet(DIN_S2_SYSTEM_CONFIG)) {
		return PSU_CFG_S2;
	} else if (bspDinGet(DIN_SOLO_SYSTEM_CONFIG)) {
		return PSU_CFG_SOLO;
	}
	return PSU_CFG_S1;
}

static bool checkMainsPresent(void)
{
	bool trolley = bspDinGet(DIN_TROLLEY_CONNECTED)
	            || bspDinGet(DIN_TROLLEY_CONNECTED_J);
	bool me_error = bspDinGet(DIN_ME_BOX_ERROR);

	return trolley && !me_error;
}
