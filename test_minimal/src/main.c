#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/dfu/mcuboot.h>

int main(void)
{
	printk("\n===== TestApp Version 0 =====\n");
	boot_write_img_confirmed();
	printk("Image confirmed, running...\n");

	uint32_t count = 0;
	while (1) {
		count++;
		printk("V0 Alive: %u\n", count);
		for (volatile uint32_t i = 0; i < 4000000; i++) {
			__asm__ volatile("nop");
		}
	}
	return 0;
}
