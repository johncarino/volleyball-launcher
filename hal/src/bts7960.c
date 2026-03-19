#include "bts7960.h"

static bool bts_initialized = false;

static int write_sysfs(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "PWM HAL: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    ssize_t len = (ssize_t)strlen(value);
    if (write(fd, value, (size_t)len) != len) {
        fprintf(stderr, "PWM HAL: write to %s failed: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static int write_long_sysfs(const char *path, long value)
{
    char val[16];
    snprintf(val, sizeof(val), "%ld", value);
    return write_sysfs(path, val);
}

static int pwm_init_one(int channel) {
    char path[128];
    
    snprintf(path, sizeof(path), "%s%d/pwm%d/enable", PWM_SYSFS_BASE, PWMCHIP, channel);
    if (write_sysfs(path, "0") < 0) { return -1; }

    snprintf(path, sizeof(path), "%s%d/pwm%d/duty_cycle", PWM_SYSFS_BASE, PWMCHIP, channel);
    if (write_sysfs(path, "0") < 0) { return -1; }

    snprintf(path, sizeof(path), "%s%d/pwm%d/period", PWM_SYSFS_BASE, PWMCHIP, channel);
    if (write_long_sysfs(path, NS_PERIOD) < 0) { return -1; } // Default 1 kHz

    return 0;
}

static int pwm_init() {
    if (bts_initialized) {
        fprintf(stderr, "BTS7960 HAL: already initialized\n");
        return -1;
    }

    if (pwm_init_one(BTS_RPWM) != 0) { return -1; }
    if (pwm_init_one(BTS_LPWM) != 0) { return -1; }

    bts_initialized = true;
    printf("BTS7960 HAL: initialized (pwmchip%d, RPWM=GPIO12/pwm1, LPWM=GPIO15/pwm0)\n", PWMCHIP);
    return 0;
}

static int pwm_set_duty(int channel, int percent) {
    char path[128];
    long duty;

    if (percent < 0 || percent > 100) {
        fprintf(stderr, "BTS7960 HAL: duty cycle %d%% out of range (0-100)\n", percent);
        return -1;
    }

    duty = (NS_PERIOD * percent) / 100;

    snprintf(path, sizeof(path), "%d/duty_cycle", channel);
    if (write_sysfs(path, "0") < 0) { return -1; }

    return 0;
}

static int enable_channel(int channel, bool enable) {
    char path[128];
    snprintf(path, sizeof(path), "%s%d/pwm%d/enable", PWM_SYSFS_BASE, PWMCHIP, channel);
    return write_sysfs(path, enable ? "1" : "0");
}

static int forward(int percent) {
    if (!bts_initialized) { return -1; }
    if (pwm_set_duty(BTS_LPWM, percent) != 0) { return -1; }
    return enable_channel(BTS_RPWM, true);
}

static int reverse(int percent) {
    if (!bts_initialized) { return -1; }
    if (pwm_set_duty(BTS_RPWM, percent) != 0) { return -1; }
    return enable_channel(BTS_LPWM, true);
}

static int forward_ms(int percent, long ms) {
    if (forward(percent) != 0) { return -1; }
    sleep_ms(ms);
    if (enable_channel(BTS_RPWM, false) < 0) { return -1};
    return 0;
}

static int reverse_ms(int percent, long ms) {
    if (reverse(percent) != 0) { return -1; }
    sleep_ms(ms);
    if (enable_channel(BTS_LPWM, false) < 0) { return -1};
    return 0;
}

static int unexport_channel(int channel) {
    char path[128];
    char val[16];
    snprintf(path, sizeof(path), "%s%d/unexport", PWM_SYSFS_BASE, PWMCHIP);
    snprintf(val, sizeof(val), "%d", channel);
    return write_sysfs(path, val);
}

void pwm_cleanup() {
    if (!bts_initialized) {return;}

    enable_channel(BTS_RPWM, false);
    set_duty_ns(BTS_RPWM, 0);
    unexport_channel(BTS_RPWM);
    enable_channel(BTS_LPWM, false);
    set_duty_ns(BTS_LPWM, 0);
    unexport_channel(BTS_LPWM);

    bts_initialized = false;
    printf("BTS7960 HAL: cleaned up\n");

}