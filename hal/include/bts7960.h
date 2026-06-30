// BTS7960 H-bridge HAL for linear actuator control.
// Uses the shared PWM HAL (pwmchip3) for motor control.
//
// Physical wiring:
//   RPWM: GPIO12 (pin 32) → pwmchip3/pwm1 → BTS_RPWM (0)
//   LPWM: GPIO15 (pin 10) → pwmchip3/pwm0 → BTS_LPWM (1)

#ifndef BTS7960_H
#define BTS7960_H

#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include "pwm.h"

int bts_init(void);
void bts_cleanup(void);

int forward_ms(int percent, long ms);
int reverse_ms(int percent, long ms);

int bts_forward_start(int percent);
int bts_reverse_start(int percent);
int bts_stop(void);

#endif // BTS7960_H