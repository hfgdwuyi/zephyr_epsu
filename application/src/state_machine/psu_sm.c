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
 *   bsp_pwm  — fan PWM control (FAN_PWM1/2)
 *   bspDoutUpdate() — status output mirroring (input→output drivers)
 */

#include "psu_sm.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include "bsp_dio.h"
#include "bsp_led.h"
#include "ntc_sensor.h"
#include "bsp_pwm.h"

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
static bool         g_state_entered = false;
static uint32_t     g_temp_ticks    = 0;      /* consecutive over-temp ms  */
static int16_t      g_temp1         = 0;      /* temp sensor 1 (×10°C)     */
static int16_t      g_temp2         = 0;      /* temp sensor 2 (×10°C)     */

/* ==================== Forward declarations ==================== */

static void state_enter(psu_state_t s);
static void state_run_init(void);
static void state_run_sys_on(void);
static void state_run_pilot_contact(void);
static void state_run_switch_on(void);
static void state_run_normal_op(void);
static void state_run_s2_mode(void);
static void state_run_charging(void);
static void state_run_shutdown(void);
static void state_run_fault(void);
static void state_run_reset(void);
static void state_run_off(void);

static psu_config_t read_config_switches(void);
static bool check_mains_present(void);
static void panel_leds_off(void);
static void panel_leds_update(psu_state_t s);
static void fan_set(bool on, uint32_t duty_percent);

/* ==================== Public API ==================== */

void psu_sm_init(void)
{
	g_state          = PSU_STATE_INIT;
	g_error          = PSU_ERR_NONE;
	g_faults         = 0;
	g_state_ticks    = 0;
	g_state_entered  = false;
	g_config         = read_config_switches();

	printk("PSU_SM: init cfg=%d\n", (int)g_config);
}

void psu_sm_tick(void)
{
	g_state_ticks++;

	if (!g_state_entered) {
		state_enter(g_state);
		g_state_entered = true;
	}

	switch (g_state) {
	case PSU_STATE_INIT:          state_run_init();          break;
	case PSU_STATE_SYS_ON:        state_run_sys_on();        break;
	case PSU_STATE_PILOT_CONTACT: state_run_pilot_contact(); break;
	case PSU_STATE_SWITCH_ON:     state_run_switch_on();     break;
	case PSU_STATE_NORMAL_OP:     state_run_normal_op();     break;
	case PSU_STATE_S2_MODE:       state_run_s2_mode();       break;
	case PSU_STATE_CHARGING:      state_run_charging();      break;
	case PSU_STATE_SHUTDOWN:      state_run_shutdown();      break;
	case PSU_STATE_FAULT:         state_run_fault();         break;
	case PSU_STATE_RESET:         state_run_reset();         break;
	case PSU_STATE_OFF:           state_run_off();           break;
	default:
		g_state = PSU_STATE_FAULT;
		break;
	}
}

psu_state_t psu_sm_get_state(void)           { return g_state; }
uint32_t   psu_sm_get_faults(void)           { return g_faults; }
psu_error_t psu_sm_get_error(void)           { return g_error; }

const char *psu_sm_get_error_str(psu_error_t err)
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

void psu_sm_request_shutdown(void)
{
	if (g_state == PSU_STATE_NORMAL_OP ||
	    g_state == PSU_STATE_S2_MODE ||
	    g_state == PSU_STATE_CHARGING) {
		printk("PSU_SM: shutdown requested\n");
		g_state = PSU_STATE_SHUTDOWN;
		g_state_entered = false;
		g_state_ticks = 0;
	}
}

void psu_sm_request_reset(void)
{
	printk("PSU_SM: reset requested\n");
	g_state = PSU_STATE_RESET;
	g_state_entered = false;
	g_state_ticks = 0;
}

void psu_sm_request_charging(void)
{
	if (g_state == PSU_STATE_NORMAL_OP) {
		printk("PSU_SM: charging requested\n");
		g_state = PSU_STATE_CHARGING;
		g_state_entered = false;
		g_state_ticks = 0;
	}
}

/* ==================== Helpers ==================== */

static void set_error(psu_error_t err)
{
	g_error = err;
	g_faults |= (uint32_t)err;
	g_state = PSU_STATE_FAULT;
	g_state_entered = false;
	g_state_ticks = 0;
}

static void transition_to(psu_state_t s)
{
	g_state = s;
	g_state_entered = false;
	g_state_ticks = 0;
}

/* ==================== Panel LED control ==================== */

static void panel_leds_off(void)
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

static void panel_leds_update(psu_state_t s)
{
	(void)s;

	if (g_state == PSU_STATE_INIT) {
		panel_leds_off();
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

static void fan_set(bool on, uint32_t duty_percent)
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
static uint32_t fan_duty_from_temp(int16_t temp_max)
{
	if (temp_max >= NTC_TEMP_FAULT)  return 100;
	if (temp_max >= NTC_TEMP_WARN)   return  80;
	if (temp_max >= NTC_TEMP_FAN_MAX) return 60;
	if (temp_max >= NTC_TEMP_FAN_MID) return 40;
	return 20;
}

/* ==================== Temperature monitoring ==================== */

static void temp_update(void)
{
	g_temp1 = ntc_read_temp(AIN_TEMP1);
	g_temp2 = ntc_read_temp(AIN_TEMP2);
}

static bool temp_overtemp(void)
{
	int16_t hi = (g_temp1 > g_temp2) ? g_temp1 : g_temp2;
	return (hi > 0) && (hi >= NTC_TEMP_FAULT);
}

/* ==================== State entry ==================== */

static void state_enter(psu_state_t s)
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
		panel_leds_off();
		fan_set(false, 0);
		ledSwitchOff(0);
		ledSwitchOff(1);
		break;

	case PSU_STATE_SYS_ON:
		bspDoutSet(DOUT_TROLLEY_ENABLE_DRV, true);
		panel_leds_update(s);
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
		panel_leds_update(s);
		fan_set(true, 50);   /* 50% initial, ramp up over time */
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
		panel_leds_update(s);
		fan_set(true, 30);   /* low power, minimal cooling */
		break;

	case PSU_STATE_CHARGING:
		panel_leds_update(s);
		fan_set(true, 70);   /* charging needs extra cooling */
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
		panel_leds_off();
		fan_set(false, 0);
		ledSwitchOff(0);
		ledSwitchOn(1);
		break;

	case PSU_STATE_OFF:
		bspDoutSet(DOUT_TROLLEY_ENABLE_DRV, false);
		bspDoutSet(DOUT_PWR_ON_OFF,         false);
		bspDoutSet(DOUT_K3_1_DRV,           false);
		bspDoutSet(DOUT_K3_2_DRV,           false);
		bspDoutSet(DOUT_K4_DRV,             false);
		panel_leds_off();
		fan_set(false, 0);
		ledSwitchOff(0);
		ledSwitchOff(1);
		break;

	default:
		break;
	}
}

/* ==================== State runners ==================== */

static void state_run_init(void)
{
	if (g_state_ticks < T_STARTUP_DELAY) {
		return;
	}

	g_config = read_config_switches();

	if (!check_mains_present()) {
		return;
	}

	transition_to(PSU_STATE_SYS_ON);
}

static void state_run_sys_on(void)
{
	if (g_state_ticks < T_CONFIG_DEBOUNCE) {
		return;
	}

	g_config = read_config_switches();

	if (!check_mains_present()) {
		set_error(PSU_ERR_MAINS_LOSS);
		return;
	}

	transition_to(PSU_STATE_PILOT_CONTACT);
}

static void state_run_pilot_contact(void)
{
	if (g_state_ticks < T_K3_WAIT) {
		return;
	}

	/* Verify pwr_on_off feedback: DIN_LED_PWR_24_ON (PC6) */
	if (!bspDinGet(DIN_LED_PWR_24_ON)) {
		if (g_state_ticks > 1000) {
			set_error(PSU_ERR_K3_TIMEOUT);
		}
		return;
	}

	transition_to(PSU_STATE_SWITCH_ON);
}

static void state_run_switch_on(void)
{
	if (g_state_ticks < T_K3_STABLE) {
		return;
	}

	if (!bspDinGet(DIN_GRID_MAIN_RELAY_STATUS)) {
		if (g_state_ticks > 2000) {
			set_error(PSU_ERR_SWITCHON_FAIL);
		}
		return;
	}

	transition_to(PSU_STATE_NORMAL_OP);
}

static void state_run_normal_op(void)
{
	if ((g_state_ticks % T_CONFIG_DEBOUNCE) == 0) {
		psu_config_t cfg = read_config_switches();
		if (cfg != g_config) {
			g_config = cfg;
			if (cfg == PSU_CFG_S2) {
				transition_to(PSU_STATE_S2_MODE);
				return;
			}
		}
	}

	if ((g_state_ticks % T_CHECKS_PERIOD) == 0) {
		if (!check_mains_present()) {
			set_error(PSU_ERR_MAINS_LOSS);
			return;
		}
		if (!bspDinGet(DIN_SYSTEM_ON_OFF)) {
			transition_to(PSU_STATE_SHUTDOWN);
			return;
		}
	}

	/* Periodic panel LED refresh */
	if ((g_state_ticks % T_CHECKS_PERIOD) == 0) {
		panel_leds_update(PSU_STATE_NORMAL_OP);
	}

	/* Temperature polling */
	if ((g_state_ticks % T_TEMP_POLL) == 0) {
		temp_update();

		if (temp_overtemp()) {
			g_temp_ticks += T_TEMP_POLL;
		} else {
			g_temp_ticks = 0;
		}

		if (g_temp_ticks >= T_TEMP_FAULT_DELAY) {
			printk("PSU_SM: over-temp fault! T1=%d.%d T2=%d.%d\n",
			       g_temp1 / 10, g_temp1 % 10,
			       g_temp2 / 10, g_temp2 % 10);
			set_error(PSU_ERR_INIT_FAIL);
			return;
		}

		/* Adjust fan speed based on temperature */
		int16_t hi = (g_temp1 > g_temp2) ? g_temp1 : g_temp2;
		if (hi > 0 && g_state_ticks > T_FAN_SPIN_UP) {
			fan_set(true, fan_duty_from_temp(hi));
		}
	}

	/* Ramp fan to full speed after spin-up (only if no temp data yet) */
	if (g_state_ticks == T_FAN_SPIN_UP && g_temp1 <= 0 && g_temp2 <= 0) {
		fan_set(true, 80);
	}
}

static void state_run_s2_mode(void)
{
	if (g_state_ticks < 50) {
		return;
	}

	if ((g_state_ticks % T_CHECKS_PERIOD) == 0) {
		psu_config_t cfg = read_config_switches();
		if (cfg != PSU_CFG_S2) {
			g_config = cfg;
			transition_to(PSU_STATE_NORMAL_OP);
			return;
		}
		if (!check_mains_present()) {
			set_error(PSU_ERR_MAINS_LOSS);
			return;
		}
		if (!bspDinGet(DIN_SYSTEM_ON_OFF)) {
			transition_to(PSU_STATE_SHUTDOWN);
			return;
		}
		panel_leds_update(PSU_STATE_S2_MODE);
	}
}

static void state_run_charging(void)
{
	if (g_state_ticks < 100) {
		return;
	}

	if ((g_state_ticks % T_CHECKS_PERIOD) == 0) {
		psu_config_t cfg = read_config_switches();
		if (cfg != g_config) {
			g_config = cfg;
			transition_to(PSU_STATE_NORMAL_OP);
			return;
		}
		if (!check_mains_present()) {
			set_error(PSU_ERR_CHARGING_FAIL);
			return;
		}
		if (!bspDinGet(DIN_SYSTEM_ON_OFF)) {
			transition_to(PSU_STATE_SHUTDOWN);
			return;
		}

		/* Temp poll every second, overtemp → fault */
		if ((g_state_ticks % T_TEMP_POLL) == 0) {
			temp_update();
			if (temp_overtemp()) { g_temp_ticks += T_TEMP_POLL; }
			else                  { g_temp_ticks = 0; }
			if (g_temp_ticks >= T_TEMP_FAULT_DELAY) {
				set_error(PSU_ERR_CHARGING_FAIL);
				return;
			}
			int16_t hi = (g_temp1 > g_temp2) ? g_temp1 : g_temp2;
			if (hi > 0)  fan_set(true, fan_duty_from_temp(hi));
		}

		panel_leds_update(PSU_STATE_CHARGING);
	}
}

static void state_run_shutdown(void)
{
	if (g_state_ticks < T_RELAY_STEP) {
		bspDoutSet(DOUT_K4_DRV, false);
	} else if (g_state_ticks < T_RELAY_STEP * 2) {
		bspDoutSet(DOUT_K3_1_DRV, false);
		bspDoutSet(DOUT_K3_2_DRV, false);
	} else if (g_state_ticks < T_RELAY_STEP * 3) {
		bspDoutSet(DOUT_PWR_ON_OFF,         false);
		bspDoutSet(DOUT_TROLLEY_ENABLE_DRV, false);
		fan_set(false, 0);
	} else if (g_state_ticks < T_RELAY_STEP * 4) {
		panel_leds_off();
		ledSwitchOff(0);
		ledSwitchOff(1);
	} else {
		transition_to(PSU_STATE_OFF);
	}
}

static void state_run_fault(void)
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
		transition_to(PSU_STATE_RESET);
		return;
	}

	/* Auto-recovery timeout */
	if (g_state_ticks > T_FAULT_RECOVER) {
		if (g_faults & (uint32_t)PSU_ERR_MAINS_LOSS) {
			if (check_mains_present()) {
				g_faults &= ~(uint32_t)PSU_ERR_MAINS_LOSS;
				g_error = PSU_ERR_RESET_RECOVERY;
				transition_to(PSU_STATE_RESET);
			}
		} else {
			g_faults = 0;
			g_error = PSU_ERR_RESET_RECOVERY;
			transition_to(PSU_STATE_RESET);
		}
	}
}

static void state_run_reset(void)
{
	if (g_state_ticks < T_RESET_HOLD) {
		return;
	}

	g_faults = 0;
	g_error = PSU_ERR_NONE;
	transition_to(PSU_STATE_INIT);
}

static void state_run_off(void)
{
	if (bspDinGet(DIN_SYSTEM_ON_OFF) && check_mains_present()) {
		transition_to(PSU_STATE_INIT);
	}
}

/* ==================== Input helpers ==================== */

static psu_config_t read_config_switches(void)
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

static bool check_mains_present(void)
{
	bool trolley = bspDinGet(DIN_TROLLEY_CONNECTED)
	            || bspDinGet(DIN_TROLLEY_CONNECTED_J);
	bool me_error = bspDinGet(DIN_ME_BOX_ERROR);

	return trolley && !me_error;
}
