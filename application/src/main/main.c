/**
 * main.c — cios-zhong PSU controller entry point
 *
 * CANopen stack (canopen/ + candriver/ + objdic/) compiled but not
 * started at runtime — NUCLEO-H745ZI-Q has no external CAN transceiver.
 * Uncomment canopenInit() / flushmbxStart() when transceiver is connected.
 */

/* Zephyr */
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>
#include <zephyr/dfu/mcuboot.h>

/* CANopen */
#define DEF_HW_PART
#include <cal_conf.h>
#include <co_init.h>
#include <co_usr.h>
#include <co_drv.h>
#include <co_drvif.h>
#include "can_zephyr.h"

/* BSP */
#include "bsp_board.h"
#include "bsp_led.h"
#include "bsp_wtdg.h"

/* Application */
#include "scheduler.h"
#include "state_machine.h"
#include "ac_meter.h"
#include "max6703a.h"

/* ---- Constants ---- */
#define CAN_BAUDRATE 500000

/* ---- CANopen FlushMbox thread (CAN message dispatch) ---- */

#define FLUSHMBX_STACK_SZ 2048
#define FLUSHMBX_PRIO     5

K_THREAD_STACK_DEFINE(flushmbx_stack, FLUSHMBX_STACK_SZ);
static struct k_thread flushmbx_thread;

static void flushMboxThreadFn(void *p1, void *p2, void *p3)
{
	while (1) {
		FlushMbox();
		Wait_For_New_Msg();
	}
}

/* ---- CANopen init wrapper ---- */

static void canopenInit(void)
{
	printk("CANopen: initializing (no CAN transceiver on NUCLEO)\n");

	/* Step 1: Init CAN controller (fdcan1, 500 kbit/s).
	 * Without a transceiver this succeeds at register level
	 * but bus communication will fail — that's expected. */
	uint8_t canRet = Init_CAN(DEVICE_DT_NAME(DT_NODELABEL(fdcan1)),
				  CAN_BAUDRATE);
	if (canRet != 0) {
		printk("CANopen: Init_CAN failed (%u)\n", (unsigned)canRet);
		return;
	}

	/* Step 2: Init CANopen protocol stack (pure software).
	 * createNodeReq() registers the NMT object but does NOT
	 * send any CAN frames yet — safe without a transceiver. */
	RET_T libRet = init_Library(CO_LINE_PARA);
	if (libRet != CO_OK) {
		printk("CANopen: init_Library failed (0x%02X)\n",
		       (unsigned)libRet);
		return;
	}

	/* Step 3: Start 1ms timer tick for CANopen stack */
	initTimer();

	/* Step 4: Start CAN controller LAST — bus communication
	 * begins here. Without a transceiver, the first TX attempt
	 * will trigger a bus-off; the driver logs it and keeps going. */
	Start_CAN();

	printk("CANopen: nodeId=%d operational\n", getNodeId());
}

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

/* ---- FlushMbox thread startup ---- */

static void flushmbxStart(void)
{
	k_tid_t tid = k_thread_create(&flushmbx_thread,
			flushmbx_stack,
			K_THREAD_STACK_SIZEOF(flushmbx_stack),
			flushMboxThreadFn,
			NULL, NULL, NULL,
			FLUSHMBX_PRIO, 0, K_NO_WAIT);
	if (tid == NULL) {
		printk("CANopen: ERROR spawning FlushMbox thread\n");
	}
}

/* ========== main() ========== */

int main(void)
{
	printk("CiosZhong PSU: app v%s / boot v%s\n",
	       CONFIG_CIOS_ZHONG_FW_VERSION, CONFIG_CIOS_ZHONG_BOOT_VERSION);

	/* MCUboot 升级确认：若本次是从 slot1 test-swap 启动的新固件，
	 * 立即标记 image-ok，固化新版本，防止下次复位被 revert 回旧版。 */
	if (!boot_is_img_confirmed()) {
		boot_write_img_confirmed();
		printk("MCUboot: image confirmed\n");
	}

	bspBoardInit();
	stateMachineInit();
	bspWtdgInit();
	max6703aInit();
	acMeterInit();

	/* LED heartbeat (also feeds MAX6703A WDI in bring-up mode) */
	heartbeatStart();

	/* ---- CANopen (disabled — no external transceiver on NUCLEO) ----
	 *   canopenInit();
	 *   flushmbxStart();
	 */

	/* ---- PSU periodic scheduler ---- */
	schedulerStart();

	return 0;
}
