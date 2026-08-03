#ifndef EXAMPLES_H
#define EXAMPLES_H

#if defined(CONFIG_DRIVER_TEST) || defined (CONFIG_TIME_TEST)

#include <zephyr/sys/printk.h>

#define PRINTF printk

#define PIN_DEV_LABEL			"GPIOB"

#endif //defined(CONFIG_DRIVER_TEST) || defined (CONFIG_TIME_TEST)

#endif //EXAMPLES_H
