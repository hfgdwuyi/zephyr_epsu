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
	/* 版本横幅统一由 uart_cmd 的启动横幅输出（含 "PSU CMD: ready"，
	 * 上位机 DFU 的 APP 就绪判定依赖该标识），此处不再重复打印版本。 */

	/* MCUboot 升级确认：若本次是从 slot1 test-swap 启动的新固件，
	 * 立即标记 image-ok，固化新版本，防止下次复位被 revert 回旧版。 */
	if (!boot_is_img_confirmed()) {
		boot_write_img_confirmed();
		printk("MCUboot: image confirmed\n");
	}

	bspBoardInit();
	stateMachineInit();
	bspWtdgInit();

	/* WWDG 已启动，但负责喂它的 wdt_sup_thread 要等 schedulerStart() 才创建。
	 * 下面的初始化（I2C/ADC/TMP75 等）若超过 WWDG 超时窗口，芯片会在启动
	 * 途中被复位导致反复重启，因此这里在关键节点手动补喂。 */
	bspWtdgFeed();

	max6703aInit();
	acMeterInit();
	bspWtdgFeed();

	tmp75Init();     /* I2C1 温度传感器（失败不阻塞启动） */
	bspWtdgFeed();

	/* LED heartbeat (also feeds MAX6703A WDI in bring-up mode) */
	heartbeatStart();

	/* ---- PSU periodic scheduler ---- */
	schedulerStart();

	return 0;
}
