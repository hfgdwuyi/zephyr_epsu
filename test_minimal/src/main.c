#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/dfu/mcuboot.h>

int main(void)
{
	printk("\n=== MINIMAL: started ===\n");
	/* TODO: re-enable after verifying boot chain */
	/* boot_write_img_confirmed(); */
	printk("=== MINIMAL: skip confirm ===\n");

	uint32_t count = 0;
	while (1) {
		count++;
		printk("Alive: %u\n", count);
		for (volatile uint32_t i = 0; i < 4000000; i++) {
			__asm__ volatile("nop");
		}
	}
	return 0;
}
