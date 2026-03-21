// BTS7960 H-bridge HAL for linear actuator control.
// Uses the shared PWM HAL (pwmchip3) for motor control.
//
// Physical wiring:
//   RPWM: GPIO12 (pin 32) → pwmchip3/pwm1 → PWM_MOTOR_1
//   LPWM: GPIO15 (pin 10) → pwmchip3/pwm0 → PWM_MOTOR_2

#include "hal/bts7960.h"

#include <stdio.h>
#include <unistd.h>

int bts_init() {
    if (pwm_init() != 0) {
        fprintf(stderr, "BTS7960 HAL: failed to initialize PWM HAL\n");
        return -1;
    }

    return 0;
}

void bts_cleanup() {
    pwm_cleanup();
}

int forward_ms(int percent, long ms) {
    if (pwm_enable(BTS_LPWM, false) != 0) { return -1; }
    if (pwm_set_duty_cycle(BTS_RPWM, percent) != 0) { return -1; }
    if (pwm_enable(BTS_RPWM, true) != 0) { return -1; }
    usleep(ms * 1000);
    if (pwm_enable(BTS_RPWM, false) != 0) { return -1; }
    return 0;
}

int reverse_ms(int percent, long ms) {
    if (pwm_enable(BTS_RPWM, false) != 0) { return -1; }
    if (pwm_set_duty_cycle(BTS_LPWM, percent) != 0) { return -1; }
    if (pwm_enable(BTS_LPWM, true) != 0) { return -1; }
    usleep(ms * 1000);
    if (pwm_enable(BTS_LPWM, false) != 0) { return -1; }
    return 0;
}
