/*
 * scheduler.c — periodic task scheduler (Zephyr implementation)
 *
 *  1 ms  : k_work_d   → bspDinUpdate()  (GPIO → bitmap)
 *  1 ms  : k_work_d   → bspDoutSet()    (bitmap → GPIO)
 *  1 ms  : k_thread   → stateMachineTick() + bspWtdgFeed()
 *  50 ms : k_work_d   → bspAinPoll()
 *  50 ms : k_work_d   → bspAoutPoll()
 *  500ms : k_work_d   → max6703aFeed()
 *  500ms : k_thread   → main.c: heartbeat (bspLedToggle)
 *  3000ms: k_work_d   → printk status
 */

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
#include "max6703a.h"

/* ========== 1 ms core thread ========== */

#define SM_STACK_SZ  2048
#define SM_PRIO      2

static struct k_thread sm_thread;
K_THREAD_STACK_DEFINE(sm_stack, SM_STACK_SZ);

static void smThreadFn(void *p1, void *p2, void *p3)
{
	while (1) {
		stateMachineTick();
		bspWtdgFeed();
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

/* ========== 1 ms: DIN sampling ========== */

static void dinWorkFn(struct k_work *w)
{
	bspDinUpdate();
	k_work_schedule(k_work_delayable_from_work(w), K_MSEC(1));
}

static K_WORK_DELAYABLE_DEFINE(din_work, dinWorkFn);

/* ========== 1 ms: DOUT commit ========== */

static void doutWorkFn(struct k_work *w)
{
	bspDoutSet();
	k_work_schedule(k_work_delayable_from_work(w), K_MSEC(1));
}

static K_WORK_DELAYABLE_DEFINE(dout_work, doutWorkFn);

/* ========== 50 ms: DAC output refresh ========== */

static void aoutWorkFn(struct k_work *w)
{
	bspAoutPoll();
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

/* ========== Start all periodic tasks ========== */

void schedulerStart(void)
{
	/* 1 ms thread */
	k_thread_create(&sm_thread, sm_stack,
			K_THREAD_STACK_SIZEOF(sm_stack),
			smThreadFn, NULL, NULL, NULL,
			SM_PRIO, 0, K_NO_WAIT);

	/* Periodic work items — each self-reschedules */
	k_work_schedule(&din_work,    K_MSEC(1));
	k_work_schedule(&dout_work,   K_MSEC(1));
	k_work_schedule(&ain_work,    K_MSEC(50));
	k_work_schedule(&aout_work,   K_MSEC(50));
	k_work_schedule(&wdi_work,    K_MSEC(500));
	k_work_schedule(&status_work, K_MSEC(3000));
}
