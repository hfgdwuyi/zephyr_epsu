/*
 * hal_debug.h — 日志输出抽象 (RTOS无关)
 *
 * 业务代码通过 hal_log() 输出调试信息。
 * 实现方负责将其映射到具体的 printf/printk/UART 等输出方式。
 */

#ifndef HAL_DEBUG_H
#define HAL_DEBUG_H

/* 由RTOS平台的printk/printf实现 */
extern int hal_log(const char *fmt, ...);

#endif /* HAL_DEBUG_H */
