/*
 * hal_gpio.h — GPIO抽象接口 (RTOS无关)
 *
 * 业务代码通过逻辑通道名访问GPIO,不直接接触物理管脚号/DTS/寄存器。
 * 实现方(zephyrproject/hal/)负责将逻辑通道映射到具体硬件。
 */

#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 逻辑输出通道 (DOUT) ==================== */

typedef enum {
	/* 继电器/功率驱动 */
	HAL_CH_TROLLEY_ENABLE_DRV,      /*  0: 手推车使能驱动      */
	HAL_CH_PWR_ON_OFF,              /*  1: 电源开关            */
	HAL_CH_K3_1_DRV,                /*  2: K3继电器通道1       */
	HAL_CH_K3_2_DRV,                /*  3: K3继电器通道2       */
	HAL_CH_K4_DRV,                  /*  4: K4继电器            */
	HAL_CH_K5_DRV,                  /*  5: K5继电器            */
	HAL_CH_K6_DRV,                  /*  6: K6继电器            */
	HAL_CH_K8_1_DRV,                /*  7: K8继电器通道1       */
	HAL_CH_K8_2_DRV,                /*  8: K8继电器通道2       */
	HAL_CH_K9_DRV,                  /*  9: K9继电器            */
	HAL_CH_K10_DRV,                 /* 10: K10继电器           */
	HAL_CH_K11_DRV,                 /* 11: K11继电器           */
	HAL_CH_K12_DRV,                 /* 12: K12继电器           */

	/* 面板LED指示灯 */
	HAL_CH_LED_S1_SYS_ON,           /* 13 */
	HAL_CH_LED_S2_SYS_ON,           /* 14 */
	HAL_CH_DBG_LED0,                /* 15 */
	HAL_CH_DBG_LED1,                /* 16 */
	HAL_CH_DBG_LED2,                /* 17 */
	HAL_CH_LED_PAC230V_ON,          /* 18 */
	HAL_CH_LED_GRID_PWR_IN,         /* 19 */
	HAL_CH_LED_UPS_IN,              /* 20 */
	HAL_CH_LED_SYSTEM_ON,           /* 21 */
	HAL_CH_LED_S2_SOLO_SYS,         /* 22 */
	HAL_CH_LED_TROLLEY_CONNECTED,   /* 23 */
	HAL_CH_LED_IS_PC_ON,            /* 24 */
	HAL_CH_LED_APPHOST_ON,          /* 25 */

	/* 状态输出驱动 */
	HAL_CH_TROLLEY_CONNECTED_MCU,   /* 26 */
	HAL_CH_TROLLEY_CONNECTED_IS_PC, /* 27 */
	HAL_CH_DRV_IS_PC_SITE_ON,       /* 28 */
	HAL_CH_DRV_APP_HOST_SITE_ON,    /* 29 */
	HAL_CH_MAINS_CONNECTED_APPHOST, /* 30 */
	HAL_CH_MAINS_CONNECTED_IS_PC,   /* 31 */

	/* 外部看门狗 (MAX6703A WDI) */
	HAL_CH_WDI,                     /* 32 */

	HAL_CH_DOUT_COUNT
} hal_dout_channel_t;

/* ==================== 逻辑输入通道 (DIN) ==================== */

typedef enum {
	HAL_CH_GRID_RELAY_STATUS,       /*  0 */
	HAL_CH_ME_BOX_ERROR,            /*  1 */
	HAL_CH_TROLLEY_CONNECTED,       /*  2 */
	HAL_CH_TEMP_ALERT,              /*  3 */
	HAL_CH_LED_PWR_24_ON,           /*  4 */
	HAL_CH_LED_CP_24V_ON,           /*  5 */
	HAL_CH_SYSTEM_ON_OFF,           /*  6 */
	HAL_CH_SYSTEM_RESET,            /*  7 */
	HAL_CH_S1_SYSTEM_CONFIG,        /*  8 */
	HAL_CH_S2_SYSTEM_CONFIG,        /*  9 */
	HAL_CH_SOLO_SYSTEM_CONFIG,      /* 10 */
	HAL_CH_TROLLEY_CONNECTED_J,     /* 11 */
	HAL_CH_IS_PC_ON,                /* 12 */
	HAL_CH_APP_HOST_ON,             /* 13 */
	HAL_CH_SMART_WHS_INDICATE,      /* 14 */
	HAL_CH_DRAWER_INDICATE,         /* 15 */
	HAL_CH_SMART_CTRL_WHS_SEARCH,   /* 16 */

	HAL_CH_DIN_COUNT
} hal_din_channel_t;

/* ==================== NUCLEO板载LED ==================== */

typedef enum {
	HAL_LED_GREEN  = 0,
	HAL_LED_YELLOW = 1,
} hal_led_t;

/* ==================== API ==================== */

/* 输出 */
void hal_gpio_out_set(hal_dout_channel_t ch, bool state);
bool hal_gpio_out_read(hal_dout_channel_t ch);

/* 输入 (单通道读取,已去抖) */
bool hal_gpio_in_get(hal_din_channel_t ch);

/* 输出镜像: 将输入状态复制到对应输出通道 (bspDoutUpdate的抽象版) */
void hal_gpio_out_mirror_inputs(void);

/* 板载LED */
void hal_led_set(hal_led_t led, bool on);
void hal_led_toggle(hal_led_t led);
void hal_led_init(void);

/* GPIO模块初始化 (在boardInit中调用) */
void hal_gpio_init(void);

/* MAX6703A WDI toggle */
void hal_wdi_feed(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_GPIO_H */
