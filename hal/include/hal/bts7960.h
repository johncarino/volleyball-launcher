#ifndef BTS7960_H
#define BTS7960_H

#include <stdbool.h>
#include "hal/pwm.h"

// BTS7960 H-bridge channel mapping to PWM HAL motors.
// RPWM = GPIO12 = pwmchip3/pwm1 = PWM_MOTOR_1
// LPWM = GPIO15 = pwmchip3/pwm0 = PWM_MOTOR_2
#define BTS_RPWM  PWM_MOTOR_1
#define BTS_LPWM  PWM_MOTOR_2

// Initialize the BTS7960 H-bridge (sets up PWM channels).
// Returns 0 on success, -1 on failure.
int bts_init(void);

// Drive forward at given duty percentage for a duration in milliseconds.
int forward_ms(int percent, long ms);

// Drive reverse at given duty percentage for a duration in milliseconds.
int reverse_ms(int percent, long ms);

// Clean up: disable PWM outputs.
void bts_cleanup(void);

#endif // BTS7960_H