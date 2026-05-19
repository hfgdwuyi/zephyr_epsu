#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/devicetree.h>

#include <stdint.h>
#include <stdbool.h>

#include "bsp_pwm.h"

#define DUTY_MAX 100U

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

#if !(DT_NODE_HAS_STATUS(ZEPHYR_USER_NODE, okay) && DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, pwms))
#error "Add 'pwms = <&... ch period flags> ...;' to /zephyr,user in app.overlay"
#endif

#define PWM_OUT_COUNT DT_PROP_LEN(ZEPHYR_USER_NODE, pwms)

struct bsp_pwm_opt {
    bool valid;
    struct pwm_dt_spec spec;
    uint32_t last_duty;
};

static struct bsp_pwm_opt pwm_out[PWM_OUT_COUNT];

/* Build-time expand all pwms entries into an array of pwm_dt_spec */
#define BSP_PWM_SPEC_ELEM(node_id, prop, idx) PWM_DT_SPEC_GET_BY_IDX(node_id, idx)

static const struct pwm_dt_spec pwm_specs[PWM_OUT_COUNT] = {
    DT_FOREACH_PROP_ELEM_SEP(ZEPHYR_USER_NODE, pwms, BSP_PWM_SPEC_ELEM, (,))
};

static inline bool pwm_idx_valid(uint8_t idx)
{
    return (idx < PWM_OUT_COUNT);
}

static void init_one(uint8_t idx, const struct pwm_dt_spec *s)
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

    /* default: keep period from DT, duty=0 */
    (void)pwm_set_dt(&pwm_out[idx].spec, pwm_out[idx].spec.period, 0U);
}

void bspPwmInit(void)
{
    for (uint8_t i = 0; i < PWM_OUT_COUNT; i++) {
        pwm_out[i].valid = false;
        pwm_out[i].last_duty = 0U;

        init_one(i, &pwm_specs[i]);
    }
}
void bspPwmStart(uint8_t pwmNum)
{
    if (!pwm_idx_valid(pwmNum) || !pwm_out[pwmNum].valid) {
        return;
    }
    /* Restore last duty cycle */
    const uint32_t period = pwm_out[pwmNum].spec.period;
    const uint32_t pulse  = (uint32_t)(((uint64_t)period * pwm_out[pwmNum].last_duty) / DUTY_MAX);
    (void)pwm_set_dt(&pwm_out[pwmNum].spec, period, pulse);
}

void bspPwmStop(uint8_t pwmNum)
{
    if (!pwm_idx_valid(pwmNum) || !pwm_out[pwmNum].valid) {
        return;
    }
    (void)pwm_set_dt(&pwm_out[pwmNum].spec, pwm_out[pwmNum].spec.period, 0U);
    pwm_out[pwmNum].last_duty = 0U;
}

void bspPwmSetCarrierFreq(uint8_t pwmNum, uint32_t frequency)
{
    if (!pwm_idx_valid(pwmNum) || !pwm_out[pwmNum].valid || frequency == 0U) {
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
    if (!pwm_idx_valid(pwmNum) || !pwm_out[pwmNum].valid) {
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