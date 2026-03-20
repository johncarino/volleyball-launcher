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

static int write_sysfs(const char *path, const char *value);

static int write_long_sysfs(const char *path, long value);

static void sleep_ms(long ms);

static int pwm_init_one(int channel);

int pwm_init();

static int pwm_set_duty(int channel, int percent);

static int enable_channel(int channel, bool enable);

static int forward(int percent);

static int reverse(int percent);

int forward_ms(int percent, long ms);

int reverse_ms(int percent, long ms);

static int unexport_channel(int channel);

void pwm_cleanup();

#endif // BTS7960_H