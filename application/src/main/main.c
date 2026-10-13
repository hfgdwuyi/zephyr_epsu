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
#include <stm32h7xx_ll_rcc.h>

/* CANopen */
#define DEF_HW_PART
#include <cal_conf.h>
#include <co_init.h>
#include <co_usr.h>
#include <co_drv.h>
#include <co_drvif.h>
#include "can_zephyr.h"
#include "canopen_status.h"

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

/* ---- Constants ---- */
#define CAN_BAUDRATE 500000

/* ---- CANopen FlushMbox thread (CAN message dispatch) ---- */

#define FLUSHMBX_STACK_SZ 2048
/* 优先级低于终端/命令线程（TERM_PRIO/CMD_PRIO = 5）：
 * 串口命令（尤其 DFU 升级）绝不能被 CANopen 分发线程抢走 CPU。*/
#define FLUSHMBX_PRIO     9

K_THREAD_STACK_DEFINE(flushmbx_stack, FLUSHMBX_STACK_SZ);
static struct k_thread flushmbx_thread;

static void flushMboxThreadFn(void *p1, void *p2, void *p3)
{
	while (1) {
		/* DFU 升级期间挂起：不处理消息，也不去等 1 ms 定时器的信号量 */
		if (canopenIsSuspended()) {
			k_sleep(K_MSEC(10));
			continue;
		}
		FlushMbox();
		Wait_For_New_Msg();
	}
}

/* ---- CANopen init wrapper ---- */

static int canopenInit(void)
{
	printk("CANopen: initializing (no CAN transceiver on NUCLEO)\n");

	/* Step 0: FDCAN 内核时钟选择。
	 * 本板无外部晶振（clk_hse disabled），而 H7 的 D2CCIP1R.FDCANSEL
	 * 复位默认是 HSE → 控制器没有内核时钟，can_start() 清不掉 CCCR.INIT
	 * 会返回 -EAGAIN（"failed to leave init mode"）。
	 * Zephyr 的 H7 时钟驱动不写这个 mux，所以在这里显式切到 PLL1Q(120 MHz)。*/
	LL_RCC_SetFDCANClockSource(LL_RCC_FDCAN_CLKSOURCE_PLL1Q);

	/* Step 1: Init CAN controller（控制器由板级 zephyr,canbus 指定，
	 * 本产品是 FDCAN2 = PB5/PB13），500 kbit/s。*/
	uint8_t canRet = Init_CAN(DEVICE_DT_NAME(DT_CHOSEN(zephyr_canbus)),
				  CAN_BAUDRATE);
	if (canRet != 0) {
		printk("CANopen: Init_CAN failed (%u)\n", (unsigned)canRet);
		return -1;
	}

	/* Step 2: Init CANopen protocol stack (pure software).
	 * createNodeReq() registers the NMT object but does NOT
	 * send any CAN frames yet — safe without a transceiver. */
	RET_T libRet = init_Library(CO_LINE_PARA);
	if (libRet != CO_OK) {
		printk("CANopen: init_Library failed (0x%02X)\n",
		       (unsigned)libRet);
		return -1;
	}

	/* Step 3: Start 1ms timer tick for CANopen stack */
	initTimer();

	/* createNodeReq() 成功后节点已处于 PRE-OPERATIONAL（库文档保证）。
	 * newStateInd() 只在后续 NMT 状态变化时被调用，所以这里先把诊断用的
	 * 状态记下来，避免 can 命令显示 "UNKNOWN (255)"。*/
	canopenNmtState = 127;   /* PRE_OPERATIONAL */

	/* Step 4: Start CAN controller LAST — bus communication
	 * begins here. Without a transceiver, the first TX attempt
	 * will trigger a bus-off; the driver logs it and keeps going. */
	Start_CAN();

	printk("CANopen: nodeId=%d operational\n", getNodeId());

	return 0;
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

	/* CANOpen stack */
	canopenInit(); 
	flushmbxStart();


	/* ---- PSU periodic scheduler ---- */
	schedulerStart();

	return 0;
}
