#ifndef CPU_GENERIC_H
#define CPU_GENERIC_H

#ifdef CONFIG_TIME_TEST
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
extern struct device *pin_dev;
/*! set time measurement bit n  */
#define CO_SET_BIT(n)		(gpio_pin_set(pin_dev, 4 + (n), 1))
/*! reset time measurement bit n */
#define CO_RESET_BIT(n)		(gpio_pin_set(pin_dev, 4 + (n), 0))

#define CO_TOGGLE_BIT(n)	(gpio_pin_toggle(pin_dev, 4 + (n)))
#else /* CONFIG_TIME_TEST */
/*! set time measurement bit n  */
#define CO_SET_BIT(n)
/*! reset time measurement bit n */
#define CO_RESET_BIT(n)

#define CO_TOGGLE_BIT(n)

#endif /* CONFIG_TIME_TEST */

#    ifndef INIT_CAN_INTERRUPTS
#      define INIT_CAN_INTERRUPTS(...)
#    endif // INIT_CAN_INTERRUPTS 
#    ifndef DISABLE_CAN_INTERRUPTS
#      define DISABLE_CAN_INTERRUPTS(...)
#    endif // DISABLE_CAN_INTERRUPTS 
#    ifndef ENABLE_CAN_INTERRUPTS
#      define ENABLE_CAN_INTERRUPTS(...)
#    endif // ENABLE_CAN_INTERRUPTS 
#    ifndef RESTORE_CAN_INTERRUPTS
#      define RESTORE_CAN_INTERRUPTS(...)
#    endif // RESTORE_CAN_INTERRUPTS 

#endif //CPU_GENERIC_H 

