/*!
 * @file wdi_feeder.c
 * @brief 极小"喂狗固件" —— 用 TIM12_CH2 的 PWM 持续翻转 PH9(WDI)
 *
 * 用途（恢复烧录，方案 C）：
 *   app 被 mass_erase 擦掉后，没人翻转 PH9 → 外部看门狗 MAX6703A（1.6 s）
 *   周期性拉 NRST → 内核 halt 不住（"timed out while waiting for target
 *   halted"）→ SWD 无法烧录，形成死循环。
 *
 *   本固件由 openocd 通过 SWD 载入 RAM(0x24000000) 并运行；它把 PH9 配成
 *   TIM12_CH2 的 PWM 输出（AF2, ~50 Hz, 50% 占空比）后就再也不需要 CPU ——
 *   定时器是硬件外设，**即使 openocd 把 M7 内核 halt 住去烧 flash，PH9 仍在
 *   被硬件翻转**，看门狗因此保持安静，烧录得以一次做完。
 *   （openocd 的 stm32h7x 配置只冻结 WWDG1/WWDG2/WDGLSD，不冻结 TIM12。）
 *
 * 载入方式（见 tools/flash_window.sh）：
 *   load_image wdi_feeder.bin 0x24000000 bin
 *   reg sp 0x24008000 ; reg pc 0x24000001 ; resume
 *
 * 无 libc、无中断、无 flash 依赖：复位后直接跑，只碰 RCC/GPIOH/TIM12。
 */
/*----------------------------------------------------------------------------*/

#include <stdint.h>

/* CMSIS 器件头要求先声明内核与型号（两个宏都由本文件自己给出，
 * 这样编译不依赖 Zephyr 构建系统） */
#define STM32H745xx
#define CORE_CM7
#include "stm32h7xx.h"

/* 复位后 M7 跑 HSI 64 MHz，APB1 分频=1 → TIM12 计数时钟 64 MHz */
#define WDI_TIMER_CLK_HZ   64000000U
#define WDI_FREQ_HZ        50U          /* 20 ms 周期，远小于 1.6 s 看门狗窗口 */
#define WDI_ARR            199U         /* 每周期 200 个计数 */

#define PH9_AF_TIM12_CH2   2U           /* PH9 的 TIM12_CH2 复用功能号 */

void _start(void)
{
	/* ---- 1. 打开 GPIOH 与 TIM12 时钟 ---- */
	RCC->AHB4ENR  |= RCC_AHB4ENR_GPIOHEN;
	RCC->APB1LENR |= RCC_APB1LENR_TIM12EN;
	(void)RCC->AHB4ENR;              /* 读回，确保写生效 */
	(void)RCC->APB1LENR;

	/* ---- 2. PH9 → 复用推挽输出（TIM12_CH2） ---- */
	GPIOH->MODER = (GPIOH->MODER & ~(3U << (9U * 2U))) | (2U << (9U * 2U));
	GPIOH->AFR[1] = (GPIOH->AFR[1] & ~(0xFU << ((9U - 8U) * 4U))) |
			((uint32_t)PH9_AF_TIM12_CH2 << ((9U - 8U) * 4U));
	GPIOH->OSPEEDR |= (2U << (9U * 2U));      /* 中速即可 */
	GPIOH->OTYPER &= ~(1U << 9U);             /* 推挽 */
	GPIOH->PUPDR = (GPIOH->PUPDR & ~(3U << (9U * 2U)));  /* 无上下拉 */

	/* ---- 3. TIM12：PWM 模式 1，CH2 输出 ~50 Hz / 50% ---- */
	TIM12->PSC   = (WDI_TIMER_CLK_HZ / (WDI_FREQ_HZ * (WDI_ARR + 1U))) - 1U;
	TIM12->ARR   = WDI_ARR;
	TIM12->CCR2  = (WDI_ARR + 1U) / 2U;                     /* 50% 占空比 */
	TIM12->CCMR1 = (TIM12->CCMR1 & ~(7U << 12U)) | (6U << 12U);  /* OC2M = PWM1 */
	TIM12->CCMR1 |= (1U << 11U);                            /* OC2PE 预装载 */
	TIM12->CCER  |= (1U << 4U);                             /* CC2E 使能输出 */
	TIM12->EGR   |= TIM_EGR_UG;                             /* 载入影子寄存器 */
	TIM12->CR1   |= TIM_CR1_CEN;                            /* 启动计数 */

	/* ---- 4. 之后 CPU 无事可做：PH9 由硬件定时器持续翻转 ---- */
	for (;;) {
		__asm__ volatile("nop");
	}
}
