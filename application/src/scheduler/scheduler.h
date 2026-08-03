/*
 * scheduler.h — periodic task scheduler
 *
 * Zephyr-specific: uses k_thread / k_work_delayable / K_TIMER.
 * Starts all periodic tasks; call scheduler_start() once from main().
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

void scheduler_start(void);

#endif
