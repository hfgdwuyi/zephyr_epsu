/**
 * main.c — cios-zhong PSU controller entry point
 *
 * CANopen stack (canopen/ + candriver/ + objdic/) compiled but not
 * started at runtime — NUCLEO-H745ZI-Q has no external CAN transceiver.
 * Uncomment canopenInit() / flushmbxStart() when transceiver is connected.
 */

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>

#include "bsp_board.h"
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

/* ---- CANopen init wrapper ---- */

static void canopenInit(void)
{
	unsigned int key;

	printk("CANopen: initializing (no CAN transceiver on NUCLEO)\n");

	key = irq_lock();

	/* Step 1: Init CAN controller (fdcan1, 500 kbit/s).
	 * Without a transceiver this succeeds at register level
	 * but bus communication will fail — that's expected. */
	uint8_t canRet = Init_CAN(DEVICE_DT_NAME(DT_NODELABEL(fdcan1)),
				  CAN_BAUDRATE);
	if (canRet != 0) {
		printk("CANopen: Init_CAN failed (%u)\n", (unsigned)canRet);
		irq_unlock(key);
		return;
	}

	/* Step 2: Init CANopen protocol stack (pure software).
	 * createNodeReq() registers the NMT object but does NOT
	 * send any CAN frames yet — safe without a transceiver. */
	RET_T libRet = init_Library(CO_LINE_PARA);
	if (libRet != CO_OK) {
		printk("CANopen: init_Library failed (0x%02X)\n",
		       (unsigned)libRet);
		irq_unlock(key);
		return;
	}

	/* Step 3: Start 1ms timer tick for CANopen stack */
	initTimer();

	/* Step 4: Start CAN controller LAST — bus communication
	 * begins here. Without a transceiver, the first TX attempt
	 * will trigger a bus-off; the driver logs it and keeps going. */
	Start_CAN();

	irq_unlock(key);

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

/* ========== main() ========== */

int main(void)
{
	printk("\n===== CiosZhong PSU v%s =====\n",
	       CONFIG_CIOS_ZHONG_FW_VERSION);

	boardInit();
	stateMachineInit();
	wtdgInit();

	/* ---- CANopen (disabled — no external transceiver on NUCLEO) ----
	 *   canopenInit();
	 *   flushmbxStart();
	 */

	/* ---- PSU periodic scheduler ---- */
	schedulerStart();

	return 0;
}
