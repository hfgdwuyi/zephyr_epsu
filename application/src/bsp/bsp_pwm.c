/*!
 * @file
 * @brief PWM outputs (Zephyr port) - cios-zhong
 *
 * Channels are described by `pwms` in the `zephyr,user` node (order = channel
 * number); the carrier period comes from that entry, and `pwm-default-duty`
 * gives the boot-time duty cycle in percent for each channel. Adding a channel
 * therefore only needs a `pwms` entry (+ its pinctrl) and a matching
 * `pwm-default-duty` value - no code change.
 */
/*----------------------------------------------------------------------------*/

/* Standard library */
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

/* pwm-default-duty: one percentage per `pwms` entry. The table is sized from the
 * devicetree property itself, and a channel without an entry falls back to 0 %
 * with a warning at init (see initOne) - so forgetting an entry is visible in
 * the boot log instead of shifting the other channels' duties. */
#if DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, pwm_default_duty)
#define PWM_DUTY_ELEM(node_id, prop, idx) DT_PROP_BY_IDX(node_id, prop, idx),
static const uint8_t pwm_default_duty[] = {
	DT_FOREACH_PROP_ELEM(ZEPHYR_USER_NODE, pwm_default_duty, PWM_DUTY_ELEM)
};
#define PWM_DUTY_TBL_LEN ARRAY_SIZE(pwm_default_duty)
#else
static const uint8_t pwm_default_duty[1] = { 0U };
#define PWM_DUTY_TBL_LEN 0U
#endif

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

static bool pwmIdxValid(uint8_t idx)
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

    /* Apply the boot-time duty from the devicetree, keeping the carrier period
     * that the `pwms` entry already configured (25 kHz on the current board). */
    if (idx >= PWM_DUTY_TBL_LEN) {
        printk("bsp_pwm: no pwm-default-duty entry for channel %u, using 0%%\n",
               (unsigned)idx);
    }
    const uint32_t want = (idx < PWM_DUTY_TBL_LEN) ? pwm_default_duty[idx] : 0U;
    const uint32_t duty = (want > DUTY_MAX) ? DUTY_MAX : want;
    const uint32_t pulse = (uint32_t)(((uint64_t)pwm_out[idx].spec.period * duty) / DUTY_MAX);

    pwm_out[idx].last_duty = duty;
    (void)pwm_set_dt(&pwm_out[idx].spec, pwm_out[idx].spec.period, pulse);
}

uint8_t bspPwmGetCount(void)
{
    return (uint8_t)PWM_OUT_COUNT;
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
