#include <zephyr/kernel.h>

/* includes  CANopen library */
#include <cal_conf.h>

#include <co_def.h>
#include <co_drv.h>
#include <co_flag.h>

#include "can_zephyr.h"
#include "cpu_generic.h"

#  ifdef CONFIG_WITHOUT_GLOBAL_VARS
/* coTimerTicks is part of the library datatype */
#  else /* CONFIG_WITHOUT_GLOBAL_VARS */
volatile u16_t  coTimerTicks CO_LINE_PARA_ARRAY_DEF; /**< CANopen timer ticks */
#  endif /* CONFIG_WITHOUT_GLOBAL_VARS */

#if CONFIG_TIMER_INC < 10
#error Zephyr cannot in timer granularity less than 1 millisecond
#endif

u16_t CO_CONST coTimerPulse = CONFIG_TIMER_INC; /**< length of 1 tick*/


static struct k_timer coTimer;


#ifdef CONFIG_COLIB_TIMER


void coTimerTickRoutine(struct k_timer *timer_id){
	ARG_UNUSED(timer_id);

#if defined(CONFIG_MULT_LINES) || defined(CONFIG_REDUNDANCY_SUPPORT)
u8_t canLine;
#endif

#ifdef CONFIG_MULT_LINES
    for (canLine = 0; canLine < CO_MAX_CAN_LINES;canLine++)
    {
		GL_ARRAY(coTimerTicks) ++; /* CANopen timer ticks */

		//SET_COLIB_FLAG_ISR(COFLAG_TIMER_PULSED);

		/* wake-up CAN-Task */
		CO_NEW_RX_MSG(CO_LINE_PARA);
    }
#else /* CONFIG_MULT_LINES */

    CO_TOGGLE_BIT(0);

    GL_ARRAY(coTimerTicks) ++; /* CANopen timer ticks line independend */

#ifdef CONFIG_REDUNDANCY_SUPPORT
    /* timer is only used for single line */
    canLine = CAN_DEFAULT_LINE;
#endif /* CONFIG_REDUNDANCY_SUPPORT */

    SET_COLIB_FLAG_ISR(COFLAG_TIMER_PULSED); /* for all CAN lines */

    /* wake-up CAN-Task */
    CO_NEW_RX_MSG(CO_LINE_PARA);
#    endif /* CONFIG_MULT_LINES */

}


/*************************************************************************/
/**
*++ \brief initTimer - initialize timer
*-- \brief initTimer - initialisiert Timer
*
*++ Resets the internal timer value to zero.
*++ Commonly it set the timer ISR to interrupt vector table.
*++ For this target it is done by the compiler.
*++ dTimerValue will only supported by compatibility.
*-- Setzt den internen Zeitzдhler auf 0.
*-- StandardmдЯig wird die Timer-ISR in die
*-- Interruptvektortabelle eingetragen.
*-- Fьr dieses System wird es durch den Compiler getan.
*-- dTimerValue wird nur aus Kompatibilitдtsgrьnden unterstьtzt.
*
* \retval 0
*++ success
*-- Erfolg
*
* \retval not 0
*++ failure
*-- Fehler
*/
u8_t initTimer(CO_GLOBVARS_PARA_DECL){


#    ifdef CONFIG_MULT_LINES
    for (u8_t canLine = 0; canLine < CO_MAX_CAN_LINES; canLine++)
#    endif /* CONFIG_MULT_LINES */
    {
    	/* with Redundancy only a line-independend coTimerTicks exists */
		GL_ARRAY(coTimerTicks) = 0; /* CANopen timer ticks */
    }

    k_timer_init(&coTimer, coTimerTickRoutine, NULL);

	k_timer_start(&coTimer, K_MSEC(coTimerPulse / CONFIG_TIMER_INC), K_MSEC(coTimerPulse / CONFIG_TIMER_INC));

	return(0);
} /* u8_t initTimer() */


/*************************************************************************/
/**
*++ \brief releaseTimer - releases the timer
*-- \brief releaseTimer - deinitialisiert den Timer
*
*
* \returns
*++ nothing
*-- nichts
*/
void releaseTimer(CO_GLOBVARS_PARA_DECL)
{
	// Stop the timer:
	k_timer_stop(&coTimer);
    return;
} /* void releaseTimer() */

#  endif /* CONFIG_COLIB_TIMER */
