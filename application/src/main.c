#include <zephyr/kernel.h>
#include "../hal/hal_debug.h"

#include <errno.h>
#include <string.h>

#include "../hal/hal_board.h"
#include "../hal/hal_adc.h"
#include "../hal/hal_gpio.h"
#include "../hal/hal_gpio.h"
#include "../hal/hal_wdt.h"
#include "psu_sm.h"

/*
 * cios-zhong periodic task schedule (Zephyr-native):
 *
 *  Thread  (1 ms):  psu_sm_tick() + hal_wdt_feed()
 *  Work   (10 ms):  hal_gpio_out_mirror_inputs()    — status output mirroring
 *  Work   (50 ms):  hal_adc_poll()       — 14 ADC channels
 *  Timer  (500 ms): NUCLEO LED toggle  — heartbeat
 *  Work   (500 ms): hal_wdi_feed()       — MAX6703A external watchdog
 *  Work   (3000 ms):status printk      — state + error + alive counter
 *
 *  DIN polling (1 ms) runs via k_timer in bspDioInit (unchanged).
 */

/* ========== 1 ms core thread: state machine + internal WDT ========== */

#define SM_THREAD_STACK_SZ  2048
#define SM_THREAD_PRIO      2    /* high priority for precise 1ms timing */

static struct k_thread sm_thread;
K_THREAD_STACK_DEFINE(sm_stack, SM_THREAD_STACK_SZ);

static void sm_thread_fn(void *p1, void *p2, void *p3)
{
	while (1) {
		psu_sm_tick();
		hal_wdt_feed();
		k_sleep(K_MSEC(1));
	}
}

/* ========== Periodic work items (self-rescheduling) ========== */

static void dout_work_fn(struct k_work *w)
{
	hal_gpio_out_mirror_inputs();
	k_work_schedule(k_work_delayable_from_work(w), K_MSEC(10));
}

static void ain_work_fn(struct k_work *w)
{
	hal_adc_poll();
	k_work_schedule(k_work_delayable_from_work(w), K_MSEC(50));
}

static void wdi_work_fn(struct k_work *w)
{
	hal_wdi_feed();
	k_work_schedule(k_work_delayable_from_work(w), K_MSEC(500));
}

static void status_work_fn(struct k_work *w)
{
	static const char *const state_names[] = {
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
	hal_log("PSU [%s] err=%s\n",
	       (s < ARRAY_SIZE(state_names)) ? state_names[s] : "?",
	       psu_sm_get_error_str(psu_sm_get_error()));

	k_work_schedule(k_work_delayable_from_work(w), K_MSEC(3000));
}

static K_WORK_DELAYABLE_DEFINE(dout_work,   dout_work_fn);
static K_WORK_DELAYABLE_DEFINE(ain_work,    ain_work_fn);
static K_WORK_DELAYABLE_DEFINE(wdi_work,    wdi_work_fn);
static K_WORK_DELAYABLE_DEFINE(status_work, status_work_fn);

/* ========== NUCLEO LED heartbeat (k_timer) ========== */

static void led_timer_fn(struct k_timer *timer)
{
	hal_led_toggle(HAL_LED_YELLOW);
}

K_TIMER_DEFINE(led_timer, led_timer_fn, NULL);

/* ========== Main: init only, no while(1) ========== */

int main(void)
{
	hal_log("\n===== CiosZhong Application v%s =====\n", BUILD_VERSION);

	hal_board_init();
	psu_sm_init();
	hal_wdt_init();

	/* 1 ms core loop thread */
	k_thread_create(&sm_thread, sm_stack,
			K_THREAD_STACK_SIZEOF(sm_stack),
			sm_thread_fn, NULL, NULL, NULL,
			SM_THREAD_PRIO, 0, K_NO_WAIT);

	/* Heartbeat LED */
	k_timer_start(&led_timer, K_MSEC(100), K_MSEC(500));

	/* Periodic work items — each self-reschedules on completion */
	k_work_schedule(&dout_work,   K_MSEC(10));
	k_work_schedule(&ain_work,    K_MSEC(50));
	k_work_schedule(&wdi_work,    K_MSEC(500));
	k_work_schedule(&status_work, K_MSEC(3000));

	return 0;
}
