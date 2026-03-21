#ifndef BTS7960_H
#define BTS7960_H

#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include "pwm.h"

// defined in pwm.h
//#define BTS_RPWM  2   // GPIO5, pwmchip3/pwm1  FORWARD
//#define BTS_LPWM  3   // GPIO14, pwmchip3/pwm0  REVERSE

int bts_init(void);
void bts_cleanup(void);

int forward_ms(int percent, long ms);
int reverse_ms(int percent, long ms);

#endif // BTS7960_H