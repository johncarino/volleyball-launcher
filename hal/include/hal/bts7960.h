#ifndef BTS7960_H
#define BTS7960_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <time.h>

// Motor identifiers
//Corresponds to the same signal
#define BTS_RPWM  1   // GPIO12, pwmchip3/pwm1
#define BTS_LPWM  0   // GPIO15, pwmchip3/pwm0

#define PWMCHIP 3
#define PWM_SYSFS_BASE  "/sys/class/pwm/pwmchip"
#define NS_PERIOD 1000000000

int bts_init(void);

int forward_ms(int percent, long ms);

int reverse_ms(int percent, long ms);

void bts_cleanup(void);

#endif // BTS7960_H