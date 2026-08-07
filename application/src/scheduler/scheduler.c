/*
 * scheduler.c — periodic task scheduler (Zephyr implementation)
 *
 *  1 ms  : k_work_d   → bspDinUpdate()  (GPIO → bitmap)  [hb]
 *  1 ms  : k_work_d   → bspDoutUpdate() (bitmap → GPIO)  [hb]
 *  1 ms  : k_thread   → stateMachineTick()               [hb]
 *  50 ms : k_work_d   → bspAinPoll()                     [hb]  (raw ADC snapshot)
 *  50 ms : k_work_d   → bspAoutPoll()                    [hb]
 *  50 ms : k_thread   → sensor: filter + convert + multi-rate publish
 *  50 ms : k_thread   → wdt supervisor: sm+sys heartbeat → bspWtdgFeed()
 *  500ms : k_work_d   → max6703aFeed()
 *  500ms : k_thread   → change-triggered sensor print (temp>3°C, volt>3V)
 *  500ms : k_thread   → main.c: heartbeat (bspLedToggle)
 *  3000ms: k_work_d   → printk status
 *
 *  [hb] marks tasks that bump a heartbeat. The WDT supervisor feeds the
 *  internal WWDG only while BOTH the state-machine heartbeat and the
 *  system heartbeat advance — a stalled state machine or task chain
 *  leaves it unfed → WWDG reset.
 */

/* C standard library */
#include <limits.h>

/* Zephyr */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* BSP */
#include "bsp_ain.h"
#include "bsp_aout.h"
#include "bsp_dio.h"
#include "bsp_wtdg.h"

/* Application */
#include "scheduler.h"
#include "state_machine.h"
#include "sensor.h"
#include "max6703a.h"

/* ========== Heartbeats + WDT supervisor ========== */

/* g_sm_heartbeat: bumped only by the state machine thread. It is the
 * primary liveness signal — the WDT supervisor will not feed unless this
 * advances, so a stalled state machine trips the watchdog even if other
 * workqueue tasks are still running.
 * g_sys_heartbeat: bumped by the other periodic [hb] tasks (DIN/DOUT/AIN/
 * AOUT), covering system-level scheduling health. */
static volatile uint32_t g_sm_heartbeat;
static volatile uint32_t g_sys_heartbeat;

static void smHbBump(void)  { g_sm_heartbeat++; }
static void sysHbBump(void) { g_sys_heartbeat++; }

/* ========== 1 ms core thread ========== */

#define SM_STACK_SZ  2048
#define SM_PRIO      2

static struct k_thread sm_thread;
K_THREAD_STACK_DEFINE(sm_stack, SM_STACK_SZ);

static void smThreadFn(void *p1, void *p2, void *p3)
{
	while (1) {
		stateMachineTick();
		smHbBump();
		k_sleep(K_MSEC(1));
	}
}

/* ========== WDT supervisor: feed only while system is alive ========== */

#define WDT_SUP_STACK_SZ  1024
#define WDT_SUP_PRIO      3

static struct k_thread wdt_sup_thread;
K_THREAD_STACK_DEFINE(wdt_sup_stack, WDT_SUP_STACK_SZ);

static void wdtSupThreadFn(void *p1, void *p2, void *p3)
{
	uint32_t last_sm  = 0;
	uint32_t last_sys = 0;

	while (1) {
		uint32_t sm  = g_sm_heartbeat;
		uint32_t sys = g_sys_heartbeat;

		/* Feed only while BOTH the state machine and the system heartbeat
		 * advanced since last check. A stalled state machine (or a stalled
		 * task chain) leaves the watchdog unfed → WWDG reset. */
		if (sm != last_sm && sys != last_sys) {
			bspWtdgFeed();
		}
		last_sm  = sm;
		last_sys = sys;

		k_sleep(K_MSEC(50));
	}
}

/* ========== 50 ms: ADC sampling ========== */

static void ainWorkFn(struct k_work *w)
{
	bspAinPoll();
	sysHbBump();
	k_work_schedule(k_work_delayable_from_work(w), K_MSEC(50));
}

static K_WORK_DELAYABLE_DEFINE(ain_work, ainWorkFn);

/* ========== 1 ms: DIN sampling ========== */

static void dinWorkFn(struct k_work *w)
{
	bspDinUpdate();
	sysHbBump();
	k_work_schedule(k_work_delayable_from_work(w), K_MSEC(1));
}

static K_WORK_DELAYABLE_DEFINE(din_work, dinWorkFn);

/* ========== 1 ms: DOUT commit ========== */

static void doutWorkFn(struct k_work *w)
{
	bspDoutUpdate();
	sysHbBump();
	k_work_schedule(k_work_delayable_from_work(w), K_MSEC(1));
}

static K_WORK_DELAYABLE_DEFINE(dout_work, doutWorkFn);

/* ========== 50 ms: DAC output refresh ========== */

static void aoutWorkFn(struct k_work *w)
{
	bspAoutPoll();
	sysHbBump();
	k_work_schedule(k_work_delayable_from_work(w), K_MSEC(50));
}

static K_WORK_DELAYABLE_DEFINE(aout_work, aoutWorkFn);

/* ========== 500 ms: MAX6703A WDI feed ========== */

static void wdiWorkFn(struct k_work *w)
{
	max6703aFeed();
	k_work_schedule(k_work_delayable_from_work(w), K_MSEC(500));
}

static K_WORK_DELAYABLE_DEFINE(wdi_work, wdiWorkFn);

/* ========== 3000 ms: status log ========== */

static void statusWorkFn(struct k_work *w)
{
	static const char *const names[] = {
		[STATEMACHINE_STATE_INIT]          = "INIT",
		[STATEMACHINE_STATE_SYS_ON]        = "SYS_ON",
		[STATEMACHINE_STATE_PILOT_CONTACT] = "PILOT",
		[STATEMACHINE_STATE_SWITCH_ON]     = "SW_ON",
		[STATEMACHINE_STATE_NORMAL_OP]     = "NORMAL",
		[STATEMACHINE_STATE_S2_MODE]       = "S2",
		[STATEMACHINE_STATE_CHARGING]      = "CHARGE",
		[STATEMACHINE_STATE_SHUTDOWN]      = "SHTDWN",
		[STATEMACHINE_STATE_FAULT]         = "FAULT",
		[STATEMACHINE_STATE_RESET]         = "RESET",
		[STATEMACHINE_STATE_OFF]           = "OFF",
	};
	stateMachineState_t s = stateMachineGetState();
	printk("PSU [%s] err=%s\n",
		(s < ARRAY_SIZE(names)) ? names[s] : "?",
		stateMachineGetErrorStr(stateMachineGetError()));

	k_work_schedule(k_work_delayable_from_work(w), K_MSEC(3000));
}

static K_WORK_DELAYABLE_DEFINE(status_work, statusWorkFn);

/* NOTE: LED heartbeat is now in main.c — heartbeatStart() runs
 * as a dedicated k_thread for proper PWM/GPIO safety on STM32H7. */

/* ========== 500 ms: change-triggered sensor print ========== */

#define DISP_STACK_SZ  1024
#define DISP_PRIO      5

/* Print only when a physical value has moved meaningfully:
 * temperature > 3.0 °C (×10), voltage > 3.0 V (mV). */
#define DISP_TEMP_DELTA  30     /* 3.0 °C, in ×10 units */
#define DISP_MV_DELTA   3000    /* 3.0 V, in mV */

static struct k_thread disp_thread;
K_THREAD_STACK_DEFINE(disp_stack, DISP_STACK_SZ);

/* Voltage channels to monitor (PDC rails + monitor rails + mains). Temps are
 * handled separately via sensorTempGet1/2. */
static const uint8_t disp_mv_chan[] = {
	AIN_ADC_PDC0, AIN_ADC_PDC1, AIN_ADC_PDC2, AIN_ADC_PDC3,
	AIN_ADC_PDC4, AIN_ADC_PDC5, AIN_ADC_PDC6, AIN_ADC_PDC7,
	AIN_ADC_PDC0_ALT,
	AIN_ADC_12V, AIN_ADC_5V0, AIN_ADC_3V3, AIN_ADC_VIN,
};

static void printTemp(const char *name, int16_t t)
{
	if (t == INT16_MIN) {
		printk("SENSOR %s: FAULT\n", name);
	} else if (t <= 0) {
		printk("SENSOR %s: n/a\n", name);
	} else {
		printk("SENSOR %s: %d.%d °C\n", name, t / 10, t % 10);
	}
}

static void dispThreadFn(void *p1, void *p2, void *p3)
{
	uint32_t last_mv[ARRAY_SIZE(disp_mv_chan)];
	int16_t  last_t1 = 0;
	int16_t  last_t2 = 0;
	bool     first = true;

	while (1) {
		int16_t t1 = sensorTempGet1();
		int16_t t2 = sensorTempGet2();

		if (first) {
			/* initial snapshot once, then only print on change */
			printTemp("temp1", t1);
			printTemp("temp2", t2);
			for (size_t i = 0; i < ARRAY_SIZE(disp_mv_chan); i++) {
				uint32_t mv = sensorGetPhys(disp_mv_chan[i]);
				last_mv[i] = mv;
				printk("SENSOR %s: %u.%03u V\n",
				       bspAinGetName(disp_mv_chan[i]),
				       mv / 1000U, mv % 1000U);
			}
			last_t1 = t1;
			last_t2 = t2;
			first = false;
			k_sleep(K_MSEC(500));
			continue;
		}

		/* temperature — print when it moved > 3 °C */
		int32_t dt1 = (int32_t)t1 - last_t1;
		int32_t dt2 = (int32_t)t2 - last_t2;
		if (dt1 < 0) dt1 = -dt1;
		if (dt2 < 0) dt2 = -dt2;
		if (dt1 >= DISP_TEMP_DELTA && t1 > 0) {
			printTemp("temp1", t1);
			last_t1 = t1;
		}
		if (dt2 >= DISP_TEMP_DELTA && t2 > 0) {
			printTemp("temp2", t2);
			last_t2 = t2;
		}

		/* voltage — print when it moved > 3 V */
		for (size_t i = 0; i < ARRAY_SIZE(disp_mv_chan); i++) {
			uint32_t mv = sensorGetPhys(disp_mv_chan[i]);
			uint32_t d = (mv > last_mv[i]) ? (mv - last_mv[i]) : (last_mv[i] - mv);
			if (d >= DISP_MV_DELTA) {
				printk("SENSOR %s: %u.%03u V\n",
				       bspAinGetName(disp_mv_chan[i]),
				       mv / 1000U, mv % 1000U);
				last_mv[i] = mv;
			}
		}

		k_sleep(K_MSEC(500));
	}
}

/* ========== Start all periodic tasks ========== */

void schedulerStart(void)
{
	/* 1 ms state machine thread */
	k_thread_create(&sm_thread, sm_stack,
			K_THREAD_STACK_SIZEOF(sm_stack),
			smThreadFn, NULL, NULL, NULL,
			SM_PRIO, 0, K_NO_WAIT);

	/* WDT supervisor thread — feeds WWDG only while heartbeat advances */
	k_thread_create(&wdt_sup_thread, wdt_sup_stack,
			K_THREAD_STACK_SIZEOF(wdt_sup_stack),
			wdtSupThreadFn, NULL, NULL, NULL,
			WDT_SUP_PRIO, 0, K_NO_WAIT);

	/* Sensor thread — filters/converts AIN raw snapshot, publishes
	 * physical values at per-quantity rates (multi-rate decimation). */
	sensorStart();

	/* Change-triggered sensor value print thread */
	k_thread_create(&disp_thread, disp_stack,
			K_THREAD_STACK_SIZEOF(disp_stack),
			dispThreadFn, NULL, NULL, NULL,
			DISP_PRIO, 0, K_NO_WAIT);

	/* Periodic work items — each self-reschedules */
	k_work_schedule(&din_work,    K_MSEC(1));
	k_work_schedule(&dout_work,   K_MSEC(1));
	k_work_schedule(&ain_work,    K_MSEC(50));
	k_work_schedule(&aout_work,   K_MSEC(50));
	k_work_schedule(&wdi_work,    K_MSEC(500));
	k_work_schedule(&status_work, K_MSEC(3000));
}
