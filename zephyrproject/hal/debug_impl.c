/*
 * debug_impl.c — Zephyr implementation of hal_debug.h
 */

#include <stdarg.h>
#include <zephyr/sys/printk.h>

int hal_log(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);
	return 0;
}
