/*
 * pwm_impl.c — Zephyr implementation of hal_pwm.h
 */

#include "../../application/hal/hal_pwm.h"
#include "bsp_pwm.h"

/* HAL logical PWM channel → BSP PWM index (1:1 for now) */

void hal_pwm_init(void)
{
	bspPwmInit();
}

void hal_pwm_set_duty(hal_pwm_channel_t ch, uint32_t duty_percent)
{
	if (ch < HAL_PWM_COUNT) {
		bspPwmSetDutyCycle((uint8_t)ch, duty_percent);
	}
}

void hal_pwm_start(hal_pwm_channel_t ch)
{
	if (ch < HAL_PWM_COUNT) {
		bspPwmStart((uint8_t)ch);
	}
}

void hal_pwm_stop(hal_pwm_channel_t ch)
{
	if (ch < HAL_PWM_COUNT) {
		bspPwmStop((uint8_t)ch);
	}
}
