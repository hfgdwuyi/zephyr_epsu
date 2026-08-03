/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/can.h>
#include <zephyr/sys/byteorder.h>

// CANOpen includes
#define DEF_HW_PART
#include <cal_conf.h>

#include <co_acces.h>
#include <co_sdo.h>
#include <co_pdo.h>
#include <co_drv.h>
#include <co_emcy.h>
#include <co_lme.h>
#include <co_nmt.h>
#include <co_init.h>
#include <co_timer.h>
#include <co_stor.h>

#include "can_zephyr.h"


/* 1000 msec = 1 sec */
#define SLEEP_TIME	2000

K_THREAD_STACK_DEFINE(thread_blinky_stack_area, 256);
static struct k_thread thread_blinky_data;

K_THREAD_STACK_DEFINE(thread_flushmbox_area, 1024);
static struct k_thread thread_flushmbox_data;

/*----------------------------------------------------------------------------*/
/*!
@brief          LED blinking thread
*/
/*----------------------------------------------------------------------------*/
void thread_blinky(void *dummy1, void *dummy2, void *dummy3){
	ARG_UNUSED(dummy1);
	ARG_UNUSED(dummy2);
	ARG_UNUSED(dummy3);
    struct device *portLed0;
    
    portLed0 = device_get_binding(DT_ALIAS_LED0_GPIOS_CONTROLLER);
    if(portLed0 == NULL){
        printk("LED device nor found\n");
        return;
    }
	/* Set LED pin as output */
	gpio_pin_configure(portLed0, DT_ALIAS_LED0_GPIOS_PIN, GPIO_OUTPUT);
	
	uint32_t cnt = 0;
	for(;;){
		gpio_pin_set(portLed0, DT_ALIAS_LED0_GPIOS_PIN, cnt++ % 2);
		k_sleep(500);
	}

}

void thread_flushmbox(void *dummy1, void *dummy2, void *dummy3){
	ARG_UNUSED(dummy1);
	ARG_UNUSED(dummy2);
	ARG_UNUSED(dummy3);

	for(;;){
		// Do the CANopen job
		FlushMbox();
		Wait_For_New_Msg();
	}

}


void main(void){
    
    printk("Demo canopen app started\n");
	printk("Compiled: %s %s for %s\n", __TIME__, __DATE__, CONFIG_BOARD);

	k_tid_t tida = k_thread_create(&thread_blinky_data, thread_blinky_stack_area, 256, thread_blinky, NULL, NULL, NULL, 7, 0, K_NO_WAIT);
    if(tida == 0){
        printk("ERROR spawning LED thread\n");
    }

	uint8_t u8Ret = Init_CAN(DT_ALIAS_CAN_PRIMARY_LABEL, 500000);
    printk("CAN init: %d\n", u8Ret);
    RET_T ret = init_Library(CO_LINE_PARA);
    if (ret != CO_OK){
    	printk("ERROR CANOpen library init: 0x%02X\n", ret);
    }
    
    Start_CAN();
    
    initTimer();
    
    k_tid_t tidf = k_thread_create(&thread_flushmbox_data, thread_flushmbox_area, 1024, thread_flushmbox, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	if(tidf == 0){
		printk("ERROR spawning LED thread\n");
	}


	while (1) {
		k_sleep(1000);
	}
}
