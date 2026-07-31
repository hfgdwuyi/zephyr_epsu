/*
 * hal_wdt.h — 看门狗抽象接口 (RTOS无关)
 */

#ifndef HAL_WDT_H
#define HAL_WDT_H

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化并启动MCU内部窗口看门狗 */
void hal_wdt_init(void);

/* 喂狗 (必须在超时窗口内调用) */
void hal_wdt_feed(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_WDT_H */
