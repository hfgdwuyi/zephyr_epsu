/**
 * main.c — cios-zhong PSU + CANopen (CiA 301 slave)
 *
 * Merged from two designs:
 * 1. Our PSU framework: stateMachine + wtdg + bsp scheduler
 * 2. Remote CANopen demo: Init_CAN → init_Library → Start_CAN →
 *    initTimer → FlushMbox thread
 *
 * CANopen Design Tool project: application/src/objdic/objdic.can
 * Object Dictionary auto-generated in application/src/objdic/objects.c
 */

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>

/* ---- Our framework ---- */
#include "bsp_board.h"
#include "bsp_led.h"
#include "bsp_wtdg.h"
#include "stateMachine.h"
#include "scheduler.h"

/* ---- CANopen stack ---- */
#define DEF_HW_PART
#include <cal_conf.h>
#include <co_init.h>
#include <co_usr.h>
#include <co_drv.h>
#include <co_drvif.h>
#include "can_zephyr.h"

/* ---- Constants ---- */
#define CAN_BAUDRATE 500000

/* ---- CANopen FlushMbox thread (CAN message dispatch) ---- */

#define FLUSHMBX_STACK_SZ 2048
#define FLUSHMBX_PRIO     5

K_THREAD_STACK_DEFINE(flushmbx_stack, FLUSHMBX_STACK_SZ);
static struct k_thread flushmbx_thread;

static void flushmbxThreadFn(void *p1, void *p2, void *p3)
{
	while (1) {
		FlushMbox();
		Wait_For_New_Msg();
	}
}

/* ---- CANopen user callbacks (required by library) ---- */

UNSIGNED8 getNodeId(void)
{
	return 1;   /* cios-zhong CANopen node ID */
}

BOOL_T canErrorInd(UNSIGNED8 flags)
{
	(void)flags;
	return 0;
}

RET_T coResetObjDirInd(UNSIGNED8 reason)
{
	(void)reason;
	return 0;
}

BOOL_T newStateInd(NODE_STATE_T st)
{
	printk("CANopen: NMT state change -> %u\n", (unsigned)st);
	return 0;
}

void resetCommInd(void)
{
	printk("CANopen: communication reset\n");
}

void resetApplInd(void)
{
	printk("CANopen: application reset\n");
}

RET_T sdoRdInd(UNSIGNED16 index, UNSIGNED8 subIdx)
{
	(void)index; (void)subIdx;
	return 0;
}

RET_T sdoWrInd(UNSIGNED16 index, UNSIGNED8 subIdx)
{
	(void)index; (void)subIdx;
	return 0;
}

/* ---- CANopen LED heartbeat (CiA 303-3) ---- */

#define COLED_STACK_SZ 512
#define COLED_PRIO     7

K_THREAD_STACK_DEFINE(coled_stack, COLED_STACK_SZ);
static struct k_thread coled_thread;

static void coledThreadFn(void *p1, void *p2, void *p3)
{
	while (1) {
		ledToggle(SYSTEM_OK_LED_NUM);
		k_sleep(K_MSEC(500));
	}
}

/* ---- CANopen init wrapper ---- */

static void canopenInit(void)
{
	printk("CANopen: initializing...\n");

	/* Init CAN controller on fdcan1 at 500 kbit/s */
	uint8_t canRet = Init_CAN(DEVICE_DT_NAME(DT_NODELABEL(fdcan1)),
				  CAN_BAUDRATE);
	if (canRet != 0) {
		printk("CANopen: Init_CAN failed (%u) — check wiring\n",
		       (unsigned)canRet);
	}

	/* Init CANopen library (objects, SDO, PDO, NMT, etc.) */
	RET_T libRet = init_Library(CO_LINE_PARA);
	if (libRet != CO_OK) {
		printk("CANopen: init_Library failed (0x%02X)\n",
		       (unsigned)libRet);
	}

	/* Start CAN controller (activate interrupts / polling) */
	Start_CAN();

	/* Start periodic timer (1 ms tick for CANopen stack) */
	initTimer();

	printk("CANopen: nodeId=%d operational\n", getNodeId());
}

/* ---- FlushMbox thread startup ---- */

static void flushmbxStart(void)
{
	k_tid_t tid = k_thread_create(&flushmbx_thread,
			flushmbx_stack,
			K_THREAD_STACK_SIZEOF(flushmbx_stack),
			flushmbxThreadFn,
			NULL, NULL, NULL,
			FLUSHMBX_PRIO, 0, K_NO_WAIT);
	if (tid == NULL) {
		printk("CANopen: ERROR spawning FlushMbox thread\n");
	}
}

/* ---- CANopen LED thread startup ---- */

static void coledStart(void)
{
	k_tid_t tid = k_thread_create(&coled_thread,
			coled_stack,
			K_THREAD_STACK_SIZEOF(coled_stack),
			coledThreadFn,
			NULL, NULL, NULL,
			COLED_PRIO, 0, K_NO_WAIT);
	if (tid == NULL) {
		printk("CANopen: ERROR spawning LED thread\n");
	}
}

/* ========== main() ========== */

int main(void)
{
	printk("\n===== CiosZhong PSU v%s =====\n",
	       CONFIG_CIOS_ZHONG_FW_VERSION);
	printk("Build: %s %s, board: %s\n",
	       __DATE__, __TIME__, CONFIG_BOARD);

	/* ---- Framework init (PSU + BSP + WDT) ---- */
	boardInit();
	stateMachineInit();
	wtdgInit();

	/* ---- CANopen stack init ---- */
	canopenInit();

	/* ---- Threads ---- */
	flushmbxStart();
	coledStart();

	/* ---- PSU periodic scheduler ---- */
	schedulerStart();

	return 0;
}
