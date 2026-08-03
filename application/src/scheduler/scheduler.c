/*
 * scheduler.c — periodic task scheduler (Zephyr implementation)
 *
 *  1 ms  : k_thread  → psu_sm_tick() + WTDG_Feed()
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

#include "psu_sm.h"

/* ========== 1 ms core thread ========== */

#define SM_STACK_SZ  2048
#define SM_PRIO      2

static struct k_thread sm_thread;
K_THREAD_STACK_DEFINE(sm_stack, SM_STACK_SZ);

static void sm_thread_fn(void *p1, void *p2, void *p3)
{
	while (1) {
		psu_sm_tick();
		WTDG_Feed();
		k_sleep(K_MSEC(1));
	}
}

/* ========== 50 ms: ADC sampling ========== */

static void ain_work_fn(struct k_work *w)
{
	bspAinPoll();
	k_work_schedule(k_work_delayable_from_work(w), K_MSEC(50));
}

static K_WORK_DELAYABLE_DEFINE(ain_work, ain_work_fn);

/* ========== 500 ms: MAX6703A WDI feed ========== */

static void wdi_work_fn(struct k_work *w)
{
	bspWdiFeed();
	k_work_schedule(k_work_delayable_from_work(w), K_MSEC(500));
}

static K_WORK_DELAYABLE_DEFINE(wdi_work, wdi_work_fn);

/* ========== 3000 ms: status log ========== */

static void status_work_fn(struct k_work *w)
{
	static const char *const names[] = {
		[PSU_STATE_INIT]          = "INIT",
		[PSU_STATE_SYS_ON]        = "SYS_ON",
		[PSU_STATE_PILOT_CONTACT] = "PILOT",
		[PSU_STATE_SWITCH_ON]     = "SW_ON",
		[PSU_STATE_NORMAL_OP]     = "NORMAL",
		[PSU_STATE_S2_MODE]       = "S2",
		[PSU_STATE_CHARGING]      = "CHARGE",
		[PSU_STATE_SHUTDOWN]      = "SHTDWN",
		[PSU_STATE_FAULT]         = "FAULT",
		[PSU_STATE_RESET]         = "RESET",
		[PSU_STATE_OFF]           = "OFF",
	};
	psu_state_t s = psu_sm_get_state();
	printk("PSU [%s] err=%s\n",
		(s < ARRAY_SIZE(names)) ? names[s] : "?",
		psu_sm_get_error_str(psu_sm_get_error()));

	k_work_schedule(k_work_delayable_from_work(w), K_MSEC(3000));
}

static K_WORK_DELAYABLE_DEFINE(status_work, status_work_fn);

/* ========== 500 ms: NUCLEO LED heartbeat ========== */

static void led_timer_fn(struct k_timer *timer)
{
	ledToggle(SYSTEM_OK_LED_NUM);
}

K_TIMER_DEFINE(led_timer, led_timer_fn, NULL);

/* ========== Start all periodic tasks ========== */

void scheduler_start(void)
{
	/* 1 ms thread */
	k_thread_create(&sm_thread, sm_stack,
			K_THREAD_STACK_SIZEOF(sm_stack),
			sm_thread_fn, NULL, NULL, NULL,
			SM_PRIO, 0, K_NO_WAIT);

	/* 500 ms LED heartbeat */
	k_timer_start(&led_timer, K_MSEC(100), K_MSEC(500));

	/* Periodic work items — each self-reschedules */
	k_work_schedule(&ain_work,    K_MSEC(50));
	k_work_schedule(&wdi_work,    K_MSEC(500));
	k_work_schedule(&status_work, K_MSEC(3000));
}
