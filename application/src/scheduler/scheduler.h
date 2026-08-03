/*
 * scheduler.h — periodic task scheduler
 *
 * Zephyr-specific: uses k_thread / k_work_delayable / K_TIMER.
 * Starts all periodic tasks; call schedulerStart() once from main().
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

void schedulerStart(void);

#endif
