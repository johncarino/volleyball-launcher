// BTS7960 H-bridge HAL for linear actuator control.
// Uses the shared PWM HAL (pwmchip3) for motor control.
//
// Physical wiring:
//   RPWM: GPIO12 (pin 32) → pwmchip3/pwm1 → PWM_MOTOR_1
//   LPWM: GPIO15 (pin 10) → pwmchip3/pwm0 → PWM_MOTOR_2

#ifndef BTS7960_H
#define BTS7960_H

#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include "pwm.h"

// defined in pwm.h
//#define BTS_RPWM  0   // GPIO12, pwmchip3/pwm1  FORWARD
//#define BTS_LPWM  1   // GPIO15, pwmchip3/pwm0  REVERSE

int bts_init(void);
void bts_cleanup(void);

int forward_ms(int percent, long ms);
int reverse_ms(int percent, long ms);

#endif // BTS7960_H