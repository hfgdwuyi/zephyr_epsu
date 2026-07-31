/*
 * hal_adc.h — ADC抽象接口 (RTOS无关)
 */

#ifndef HAL_ADC_H
#define HAL_ADC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 模拟输入逻辑通道 */
typedef enum {
	HAL_AIN_ADC_3V3,        /*  0: PF6  */
	HAL_AIN_ADC_PDC1,       /*  1: PF7  */
	HAL_AIN_ADC_PDC7,       /*  2: PF8  */
	HAL_AIN_ADC_PDC6,       /*  3: PF9  */
	HAL_AIN_ADC_PDC5,       /*  4: PF10 */
	HAL_AIN_ADC_TEMP1,      /*  5: PA3  — NTC sensor 1 */
	HAL_AIN_ADC_TEMP2,      /*  6: PA4  — NTC sensor 2 */
	HAL_AIN_ADC_PDC0,       /*  7: PA6  */
	HAL_AIN_ADC_PDC4,       /*  8: PC0  */
	HAL_AIN_ADC_PDC2,       /*  9: PB0  */
	HAL_AIN_ADC_PDC3,       /* 10: PB1  */
	HAL_AIN_ADC_VIN,        /* 11: PC2  */
	HAL_AIN_ADC_PDC0_ALT,   /* 12: PC3  */
	HAL_AIN_ADC_5V,         /* 13: PH4  */

	HAL_AIN_COUNT
} hal_ain_channel_t;

/* 初始化所有ADC通道 */
void hal_adc_init(void);

/* 轮询采样所有通道 */
void hal_adc_poll(void);

/* 读取最近一次采样值 (12-bit raw, 0-4095) */
uint32_t hal_adc_read_raw(hal_ain_channel_t ch);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ADC_H */
