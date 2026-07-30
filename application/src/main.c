#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <errno.h>
#include <string.h>

#include "bsp_board.h"
#include "bsp_ain.h"
#include "bsp_dio.h"
#include "bsp_led.h"
#include "bsp_wtdg.h"
#include "psu_sm.h"

/*
 * cios-zhong periodic task schedule:
 *
 *  1 ms : psu_sm_tick()     — state machine + relay sequencing
 *  1 ms : WTDG_Feed()       — watchdog kick
 *  1 ms : bspDinUpdate()    — DIN polling (background k_timer, started in boardInit)
 * 10 ms : bspDoutUpdate()   — status output mirroring (input → output drivers)
 * 50 ms : bspAinPoll()      — 14 ADC channels
 * 50 ms : panel LED refresh — via psu_sm_tick → state runner → panel_leds_update
 * 500ms : LED toggle        — NUCLEO yellow LED heartbeat (background k_timer)
 * 500ms : fan PWM update    — via psu_sm_tick → state runner → fan_set
 * 3000ms: status printk     — current state
 */

/* ---------- LED blink timer ---------- */
static struct k_timer led_timer;

static void led_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	ledToggle(SYSTEM_OK_LED_NUM);
}

/* ---------- Main ---------- */
int main(void)
{
	printk("\n===== CiosZhong Application v%s =====\n", BUILD_VERSION);

	boardInit();
	psu_sm_init();
	WTDG_Init();

	k_timer_init(&led_timer, led_timer_handler, NULL);
	k_timer_start(&led_timer, K_MSEC(100), K_MSEC(500));

	uint32_t count = 0;
	while (1) {
		/* ---- 1 ms tasks ---- */
		psu_sm_tick();
		WTDG_Feed();

		/* ---- 10 ms tasks ---- */
		if ((count % 10) == 0) {
			bspDoutUpdate();
		}

		/* ---- 50 ms tasks ---- */
		if ((count % 50) == 0) {
			bspAinPoll();
		}

		/* ---- 3000 ms tasks ---- */
		if ((count % 3000) == 0) {
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
			printk("PSU [%s] err=%s count=%u\n",
			       (s < ARRAY_SIZE(state_names)) ? state_names[s] : "?",
			       psu_sm_get_error_str(psu_sm_get_error()),
			       count);
		}

		count++;
		k_sleep(K_MSEC(1));
	}
	return 0;
}
