/*
 * scheduler.c — periodic task scheduler (Zephyr implementation)
 *
 *  1 ms  : k_thread  → stateMachineTick() + wtdgFeed()
 *  10 ms : k_timer    → DOUT status mirror (bsp_dio internal, auto-started)
 *  50 ms : k_work_d   → bspAinPoll()
 *  500ms : k_work_d   → bspWdiFeed()
 *  500ms : K_TIMER    → ledToggle(SYSTEM_OK_LED_NUM)
 *  3000ms: k_work_d   → hal_log status
 */

#include "scheduler.h"

#include <zephyr/kernel.h>

#include <zephyr/sys/printk.h>

#include "bsp_dio.h"
#include "bsp_ain.h"
#include "bsp_led.h"
#include "bsp_wtdg.h"

#include "stateMachine.h"

/* ========== 1 ms core thread ========== */

#define SM_STACK_SZ  2048
#define SM_PRIO      2

static struct k_thread sm_thread;
K_THREAD_STACK_DEFINE(sm_stack, SM_STACK_SZ);

static void smThreadFn(void *p1, void *p2, void *p3)
{
	while (1) {
		stateMachineTick();
		wtdgFeed();
		k_sleep(K_MSEC(1));
	}
}

/* ========== 50 ms: ADC sampling ========== */

static void ainWorkFn(struct k_work *w)
{
	bspAinPoll();
	k_work_schedule(k_work_delayable_from_work(w), K_MSEC(50));
}

static K_WORK_DELAYABLE_DEFINE(ain_work, ainWorkFn);

/* ========== 500 ms: MAX6703A WDI feed ========== */

static void wdiWorkFn(struct k_work *w)
{
	bspWdiFeed();
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

/* ========== 500 ms: NUCLEO LED heartbeat ========== */

static void ledTimerFn(struct k_timer *timer)
{
	ledToggle(SYSTEM_OK_LED_NUM);
}

K_TIMER_DEFINE(led_timer, ledTimerFn, NULL);

/* ========== Start all periodic tasks ========== */

void schedulerStart(void)
{
	/* 1 ms thread */
	k_thread_create(&sm_thread, sm_stack,
			K_THREAD_STACK_SIZEOF(sm_stack),
			smThreadFn, NULL, NULL, NULL,
			SM_PRIO, 0, K_NO_WAIT);

	/* 500 ms LED heartbeat */
	k_timer_start(&led_timer, K_MSEC(100), K_MSEC(500));

	/* Periodic work items — each self-reschedules */
	k_work_schedule(&ain_work,    K_MSEC(50));
	k_work_schedule(&wdi_work,    K_MSEC(500));
	k_work_schedule(&status_work, K_MSEC(3000));
}
