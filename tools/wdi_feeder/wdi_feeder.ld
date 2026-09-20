/* wdi_feeder.ld — 喂狗固件的链接脚本
 *
 * 目标地址 0x24000000 = AXI SRAM(D1) 起始，openocd 用
 *   load_image wdi_feeder.bin 0x24000000 bin
 * 把二进制塞进去，再把 PC/SP 指向这里运行。
 * 不需要 .data/.bss（代码只用寄存器，无全局变量），也不依赖 flash。
 */
ENTRY(_start)

MEMORY
{
	RAM (rwx) : ORIGIN = 0x24000000, LENGTH = 64K
}

SECTIONS
{
	.text : {
		KEEP(*(.text._start))
		*(.text*)
		*(.rodata*)
	} > RAM

	/DISCARD/ : {
		*(.ARM.attributes)
		*(.comment)
		*(.data*)
		*(.bss*)
	}
}
