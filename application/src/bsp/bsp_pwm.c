/*!
 * @file
 * @brief Fan PWM control (Zephyr port) — cios-zhong
 *
 * Drives the fan PWM channels (TIM2/PJ15, TIM8/PI15) via the Zephyr PWM
 * driver. Carrier frequency and duty cycle are set per channel; the fan
 * speed policy itself lives in the state machine.
 */
/*----------------------------------------------------------------------------*/

/* C standard library */
#include <stdbool.h>
#include <stdint.h>

/* Zephyr */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>

/* BSP */
#include "bsp_pwm.h"

#define DUTY_MAX 100U

/* PWM is fixed on this platform (H745) — pwms always present. */
#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)
#define PWM_OUT_COUNT DT_PROP_LEN(ZEPHYR_USER_NODE, pwms)

typedef struct {
    bool valid;
    struct pwm_dt_spec spec;
    uint32_t last_duty;
} bspPwmOpt_t;

static bspPwmOpt_t pwm_out[PWM_OUT_COUNT];

#define BSP_PWM_SPEC_ELEM(node_id, prop, idx) PWM_DT_SPEC_GET_BY_IDX(node_id, idx)

static const struct pwm_dt_spec pwm_specs[PWM_OUT_COUNT] = {
    DT_FOREACH_PROP_ELEM_SEP(ZEPHYR_USER_NODE, pwms, BSP_PWM_SPEC_ELEM, (,))
};

static inline bool pwmIdxValid(uint8_t idx)
{
    return (idx < PWM_OUT_COUNT);
}

static void initOne(uint8_t idx, const struct pwm_dt_spec *s)
{
    pwm_out[idx].valid = false;
    pwm_out[idx].last_duty = 0U;

    if (!s || !s->dev) {
        return;
    }
    if (!pwm_is_ready_dt(s)) {
        return;
    }

    pwm_out[idx].spec = *s;
    pwm_out[idx].valid = true;
    (void)pwm_set_dt(&pwm_out[idx].spec, pwm_out[idx].spec.period, 0U);
}

void bspPwmInit(void)
{
    for (uint8_t i = 0; i < PWM_OUT_COUNT; i++) {
        pwm_out[i].valid = false;
        pwm_out[i].last_duty = 0U;
        initOne(i, &pwm_specs[i]);
    }
}

void bspPwmStart(uint8_t pwmNum)
{
    if (!pwmIdxValid(pwmNum) || !pwm_out[pwmNum].valid) {
        return;
    }
    const uint32_t period = pwm_out[pwmNum].spec.period;
    const uint32_t pulse  = (uint32_t)(((uint64_t)period * pwm_out[pwmNum].last_duty) / DUTY_MAX);
    (void)pwm_set_dt(&pwm_out[pwmNum].spec, period, pulse);
}

void bspPwmStop(uint8_t pwmNum)
{
    if (!pwmIdxValid(pwmNum) || !pwm_out[pwmNum].valid) {
        return;
    }
    (void)pwm_set_dt(&pwm_out[pwmNum].spec, pwm_out[pwmNum].spec.period, 0U);
    pwm_out[pwmNum].last_duty = 0U;
}

void bspPwmSetCarrierFreq(uint8_t pwmNum, uint32_t frequency)
{
    if (!pwmIdxValid(pwmNum) || !pwm_out[pwmNum].valid || frequency == 0U) {
        return;
    }
    const uint32_t duty = pwm_out[pwmNum].last_duty;
    const uint32_t period = (uint32_t)(1000000000ULL / (uint64_t)frequency);
    const uint32_t pulse  = (uint32_t)(((uint64_t)period * duty) / DUTY_MAX);
    pwm_out[pwmNum].spec.period = period;
    (void)pwm_set_dt(&pwm_out[pwmNum].spec, period, pulse);
}

void bspPwmSetDutyCycle(uint8_t pwmNum, uint32_t value)
{
    if (!pwmIdxValid(pwmNum) || !pwm_out[pwmNum].valid) {
        return;
    }
    if (value > DUTY_MAX) {
        value = DUTY_MAX;
    }
    pwm_out[pwmNum].last_duty = value;
    const uint32_t period = pwm_out[pwmNum].spec.period;
    const uint32_t pulse  = (uint32_t)(((uint64_t)period * value) / DUTY_MAX);
    (void)pwm_set_dt(&pwm_out[pwmNum].spec, period, pulse);
}

/* Current duty cycle (%) — status query for host tools */
uint32_t bspPwmGetDutyCycle(uint8_t pwmNum)
{
    if (!pwmIdxValid(pwmNum)) {
        return 0U;
    }
    return pwm_out[pwmNum].last_duty;
}