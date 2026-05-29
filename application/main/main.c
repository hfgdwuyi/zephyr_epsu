#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/net/net_config.h>

#include <errno.h>
#include <string.h>

#include "dm_api.h"
#include "bsp_board.h"
#include "bsp_led.h"
#include "http_api.h"

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
	printk("\n===== Application v%s =====\n", BUILD_VERSION);
	boot_write_img_confirmed();
	printk("Image confirmed, running...\n");

	dm_init();
	boardInit();

	/* Start LED blink: 500ms period = system alive indicator */
	k_timer_init(&led_timer, led_timer_handler, NULL);
	k_timer_start(&led_timer, K_MSEC(100), K_MSEC(500));

	/* Init network (static IP 192.0.2.1) */
	printk("Initializing network...\n");
	(void)net_config_init_app(NULL, "Initializing network");
	printk("Network ready\n");

	/* Give network stack time to stabilize */
	k_sleep(K_SECONDS(2));

	/* Start HTTP server (uses full REST framework from lib/http) */
	http_api_start();
	printk("HTTP server listening on port 80\n");

	uint32_t count = 0;
	while (1) {
		count++;
		printk("Application v%s Alive: %u\n", BUILD_VERSION, count);
		k_sleep(K_SECONDS(3));
	}
	return 0;
}
