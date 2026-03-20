// BTS7960 H-bridge HAL for linear actuator control.
// Uses the shared PWM HAL (pwmchip3) for motor control.
//
// Physical wiring:
//   RPWM: GPIO12 (pin 32) → pwmchip3/pwm1 → PWM_MOTOR_1
//   LPWM: GPIO15 (pin 10) → pwmchip3/pwm0 → PWM_MOTOR_2

#include "hal/bts7960.h"

#include <stdio.h>
#include <time.h>

static bool bts_initialized = false;

static void sleep_ms(long ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

int bts_init(void)
{
    if (bts_initialized) {
        fprintf(stderr, "BTS7960 HAL: already initialized\n");
        return -1;
    }

    if (pwm_init() != 0) {
        fprintf(stderr, "BTS7960 HAL: PWM init failed\n");
        return -1;
    }

    bts_initialized = true;
    printf("BTS7960 HAL: initialized (RPWM=PWM_MOTOR_1/GPIO12, LPWM=PWM_MOTOR_2/GPIO15)\n");
    return 0;
}

static int forward(int percent)
{
    if (!bts_initialized) { return -1; }
    if (pwm_set_duty_cycle(BTS_LPWM, percent) != 0) { return -1; }
    return pwm_enable(BTS_RPWM, true);
}

static int reverse(int percent)
{
    if (!bts_initialized) { return -1; }
    if (pwm_set_duty_cycle(BTS_RPWM, percent) != 0) { return -1; }
    return pwm_enable(BTS_LPWM, true);
}

int forward_ms(int percent, long ms)
{
    if (forward(percent) != 0) { return -1; }
    sleep_ms(ms);
    if (pwm_enable(BTS_RPWM, false) < 0) { return -1; }
    return 0;
}

int reverse_ms(int percent, long ms)
{
    if (reverse(percent) != 0) { return -1; }
    sleep_ms(ms);
    if (pwm_enable(BTS_LPWM, false) < 0) { return -1; }
    return 0;
}

void bts_cleanup(void)
{
    if (!bts_initialized) { return; }

    pwm_enable(BTS_RPWM, false);
    pwm_set_duty_cycle(BTS_RPWM, 0);
    pwm_enable(BTS_LPWM, false);
    pwm_set_duty_cycle(BTS_LPWM, 0);
    pwm_cleanup();

    bts_initialized = false;
    printf("BTS7960 HAL: cleaned up\n");
}