/*!
 * Copyright © Siemens Healthcare GmbH 2022, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 * @file
 * @brief Header file for timing.c
 */
/*----------------------------------------------------------------------------*/
#ifndef TIMING_H
#define TIMING_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

/*! Timer used to implement time delays */
#define TIMING_TIMER_DELAY 0
/*! Timer to be run one time */
#define TIMING_TIMER_ONE_SHOT 1
/*! Periodic timer */
#define TIMING_TIMER_CYCLIC 2

/* Typedef for handler to be called after a timer is expired */
typedef void (*timingCallback)(void);

/* Structure for timers to be added to linked list */
typedef struct timing_Timer
{
    /* Legacy fields (kept for source compatibility; not used by Zephyr backend) */
    struct timing_Timer *pNext;     /*!< Pointer to the next timer in the list */
    uint32_t             timeout;   /*!< Timer's timeout (ms) */
    volatile uint32_t    counter;   /*!< Legacy counter (ms tick based) */
    uint8_t              type;      /*!< Timer's type */
    timingCallback       callback;  /*!< Callback */

    /* Zephyr backend fields */
    struct k_timer       z_timer;
    struct k_work        z_work;
    atomic_t             z_expired;     /* 0: running, 1: expired/not active */
    atomic_t             z_initialized; /* 0: no, 1: yes */
} timingTimer;

static inline void timingDelay_us(uint32_t delay_us)
{
    /* In Zephyr, busy wait uses microseconds */
    if (delay_us == 0U) {
        return;
    }
    k_busy_wait(delay_us);
}

void timingInit(void);
void timingRemoveTimer(const timingTimer *pTimer);
void timingAddTimer(timingTimer *pTimer, uint8_t type, uint32_t timeout, timingCallback callback);
void timingExecute(void);
bool timingCheckTimeout(const timingTimer *pTimer);
void timingTick(void);
void timingDelay_ms(uint32_t delay_ms);

#endif

//--------------------------------- End Of File -------------------------------/