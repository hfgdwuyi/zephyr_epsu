/**
 * main.c — cios-zhong PSU controller entry point
 */

/* Zephyr */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/dfu/mcuboot.h>

/* BSP */
#include "bsp_board.h"
#include "bsp_led.h"
#include "bsp_wtdg.h"

/* Application */
#include "scheduler.h"
#include "state_machine.h"
#include "ac_meter.h"
#include "max6703a.h"
#include "tmp75.h"

/* ---- Heartbeat LED thread ---- */

static void heartbeatThreadFn(void *p1, void *p2, void *p3)
{
	while (1) {
		bspLedToggle(SYSTEM_OK_LED_NUM);  

		k_sleep(K_MSEC(200));
	}
}

#define HEARTBEAT_STACK_SZ 512
#define HEARTBEAT_PRIO     7

K_THREAD_STACK_DEFINE(heartbeatStack, HEARTBEAT_STACK_SZ);
static struct k_thread heartbeatThread;

static void heartbeatStart(void)
{
	k_tid_t tid = k_thread_create(&heartbeatThread,
			heartbeatStack,
			K_THREAD_STACK_SIZEOF(heartbeatStack),
			heartbeatThreadFn,
			NULL, NULL, NULL,
			HEARTBEAT_PRIO, 0, K_NO_WAIT);
	if (tid == NULL) {
		printk("ERROR spawning heartbeat LED thread\n");
	}
}

/* ========== main() ========== */

int main(void)
{
	/* MCUboot confirm: mark the running image as OK so it is not reverted. */
	if (!boot_is_img_confirmed()) {
		boot_write_img_confirmed();
		printk("MCUboot: image confirmed\n");
	}

	bspBoardInit();
	stateMachineInit();
	bspWtdgInit();

	bspWtdgFeed();

	max6703aInit();
	acMeterInit();
	
	bspWtdgFeed();

	tmp75Init();     
	bspWtdgFeed();

	/* LED heartbeat */
	heartbeatStart();

	/* ---- PSU periodic scheduler ---- */
	schedulerStart();

	return 0;
}
