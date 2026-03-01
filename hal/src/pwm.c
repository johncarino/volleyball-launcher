// PWM HAL implementation using Linux sysfs for BeagleY-AI.
// Controls DC motor speed via ZS-X11H driver board.
// Only P (PWM signal) and G (ground) are connected.

#include "hal/pwm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------
static int s_pwmchip = -1;
static int s_channel = -1;
static int s_period_ns = 0;       // current period in nanoseconds
static int s_duty_ns = 0;         // current duty cycle in nanoseconds
static int s_duty_percent = 0;
static int s_frequency_hz = 0;
static bool s_initialized = false;

#define PWM_SYSFS_BASE  "/sys/class/pwm/pwmchip"
#define NS_PER_SECOND   1000000000

// ---------------------------------------------------------------------------
// Helper: write a string to a sysfs file
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int pwm_init(int pwmchip, int channel)
{
    if (s_initialized) {
        fprintf(stderr, "PWM HAL: already initialized\n");
        return -1;
    }

    s_pwmchip = pwmchip;
    s_channel = channel;

    char path[128];
    char val[16];

    // Export the PWM channel (skip if already exported)
    snprintf(path, sizeof(path), "%s%d/pwm%d/enable",
             PWM_SYSFS_BASE, s_pwmchip, s_channel);
    if (access(path, F_OK) != 0) {
        snprintf(path, sizeof(path), "%s%d/export", PWM_SYSFS_BASE, s_pwmchip);
        snprintf(val, sizeof(val), "%d", s_channel);
        if (write_sysfs(path, val) != 0) {
            fprintf(stderr, "PWM HAL: failed to export pwmchip%d channel %d\n",
                    s_pwmchip, s_channel);
            return -1;
        }
        // Wait for sysfs entries to appear
        usleep(100000);
    }

    // Set default frequency: 1 kHz
    s_frequency_hz = 1000;
    s_period_ns = NS_PER_SECOND / s_frequency_hz;
    s_duty_ns = 0;
    s_duty_percent = 0;

    // Write period
    snprintf(path, sizeof(path), "%s%d/pwm%d/period",
             PWM_SYSFS_BASE, s_pwmchip, s_channel);
    snprintf(val, sizeof(val), "%d", s_period_ns);
    if (write_sysfs(path, val) != 0) {
        return -1;
    }

    // Write duty cycle = 0 (motor stopped)
    snprintf(path, sizeof(path), "%s%d/pwm%d/duty_cycle",
             PWM_SYSFS_BASE, s_pwmchip, s_channel);
    if (write_sysfs(path, "0") != 0) {
        return -1;
    }

    s_initialized = true;
    printf("PWM HAL: initialized (pwmchip%d/pwm%d, freq=%d Hz)\n",
           s_pwmchip, s_channel, s_frequency_hz);
    return 0;
}

int pwm_set_frequency(int frequency_hz)
{
    if (!s_initialized) {
        return -1;
    }
    if (frequency_hz < 50 || frequency_hz > 20000) {
        fprintf(stderr, "PWM HAL: frequency %d Hz out of range (50-20000)\n",
                frequency_hz);
        return -1;
    }

    s_frequency_hz = frequency_hz;
    s_period_ns = NS_PER_SECOND / frequency_hz;

    // Recalculate duty in ns to keep the same percentage
    s_duty_ns = s_period_ns * s_duty_percent / 100;

    char path[128];
    char val[16];

    // Duty must be <= period, so zero it before changing period
    snprintf(path, sizeof(path), "%s%d/pwm%d/duty_cycle",
             PWM_SYSFS_BASE, s_pwmchip, s_channel);
    write_sysfs(path, "0");

    // Set new period
    snprintf(path, sizeof(path), "%s%d/pwm%d/period",
             PWM_SYSFS_BASE, s_pwmchip, s_channel);
    snprintf(val, sizeof(val), "%d", s_period_ns);
    if (write_sysfs(path, val) != 0) {
        return -1;
    }

    // Restore duty cycle
    snprintf(path, sizeof(path), "%s%d/pwm%d/duty_cycle",
             PWM_SYSFS_BASE, s_pwmchip, s_channel);
    snprintf(val, sizeof(val), "%d", s_duty_ns);
    if (write_sysfs(path, val) != 0) {
        return -1;
    }

    printf("PWM HAL: frequency set to %d Hz (period = %d ns)\n",
           frequency_hz, s_period_ns);
    return 0;
}

int pwm_set_duty_cycle(int duty_percent)
{
    if (!s_initialized) {
        return -1;
    }
    if (duty_percent < 0 || duty_percent > 100) {
        fprintf(stderr, "PWM HAL: duty cycle %d%% out of range (0-100)\n",
                duty_percent);
        return -1;
    }

    s_duty_percent = duty_percent;
    s_duty_ns = s_period_ns * duty_percent / 100;

    char path[128];
    char val[16];
    snprintf(path, sizeof(path), "%s%d/pwm%d/duty_cycle",
             PWM_SYSFS_BASE, s_pwmchip, s_channel);
    snprintf(val, sizeof(val), "%d", s_duty_ns);
    if (write_sysfs(path, val) != 0) {
        return -1;
    }

    printf("PWM HAL: duty cycle set to %d%% (%d ns)\n", duty_percent, s_duty_ns);
    return 0;
}

int pwm_enable(bool enable)
{
    if (!s_initialized) {
        return -1;
    }

    char path[128];
    snprintf(path, sizeof(path), "%s%d/pwm%d/enable",
             PWM_SYSFS_BASE, s_pwmchip, s_channel);
    if (write_sysfs(path, enable ? "1" : "0") != 0) {
        return -1;
    }

    printf("PWM HAL: %s\n", enable ? "enabled" : "disabled");
    return 0;
}

int pwm_get_duty_cycle(void)
{
    return s_duty_percent;
}

int pwm_get_frequency(void)
{
    return s_frequency_hz;
}

void pwm_cleanup(void)
{
    if (!s_initialized) {
        return;
    }

    // Disable PWM output
    pwm_enable(false);

    // Zero duty cycle
    char path[128];
    snprintf(path, sizeof(path), "%s%d/pwm%d/duty_cycle",
             PWM_SYSFS_BASE, s_pwmchip, s_channel);
    write_sysfs(path, "0");

    // Unexport PWM channel
    char val[16];
    snprintf(path, sizeof(path), "%s%d/unexport", PWM_SYSFS_BASE, s_pwmchip);
    snprintf(val, sizeof(val), "%d", s_channel);
    write_sysfs(path, val);

    s_initialized = false;
    printf("PWM HAL: cleaned up\n");
}
