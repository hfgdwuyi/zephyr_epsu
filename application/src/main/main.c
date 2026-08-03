/* main.c — cios-zhong entry point. Init hardware + state machine,
 * then hand off periodic tasks to the scheduler and return to Zephyr. */

#include <zephyr/sys/printk.h>

#include "bsp_board.h"
#include "bsp_wtdg.h"


#include "psu_sm.h"
#include "scheduler.h"

int main(void)
{
	printk("\n===== CiosZhong Application v%s =====\n",
		CONFIG_CIOS_ZHONG_FW_VERSION);

	boardInit();
	psu_sm_init();
	WTDG_Init();
	scheduler_start();

	return 0;
}
