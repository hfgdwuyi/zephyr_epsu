/*
 * hal_pwm.h — PWM抽象接口 (RTOS无关)
 */

#ifndef HAL_PWM_H
#define HAL_PWM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PWM逻辑通道 */
typedef enum {
	HAL_PWM_FAN1,       /* 0: PJ15, TIM2_CH4 */
	HAL_PWM_FAN2,       /* 1: PI15, TIM8_CH3 */

	HAL_PWM_COUNT
} hal_pwm_channel_t;

void hal_pwm_init(void);
void hal_pwm_set_duty(hal_pwm_channel_t ch, uint32_t duty_percent);   /* 0-100 */
void hal_pwm_start(hal_pwm_channel_t ch);
void hal_pwm_stop(hal_pwm_channel_t ch);

#ifdef __cplusplus
}
#endif

#endif /* HAL_PWM_H */
