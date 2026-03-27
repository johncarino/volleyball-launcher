// PWM HAL implementation using Linux sysfs for BeagleY-AI.
// Both on EPWM0 (pwmchip3): channel A (pwm0) and channel B (pwm1).
//
// Physical wiring:
//   Motor 1: GPIO12 (pin 32) → pwmchip3/pwm1 (channel B)
//   Motor 2: GPIO15 (pin 10) → pwmchip3/pwm0 (channel A)

#include "hal/pwm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// Hardware mapping
#define PWMCHIP         3
#define MOTOR1_CHANNEL  1   // pwm1 = channel B = GPIO12
#define MOTOR2_CHANNEL  0   // pwm0 = channel A = GPIO15

// USED IN BTS7960.C
#define BTS_RPWM_CHANNEL  2   // GPIO5, pwmchip3/pwm1
#define BTS_LPWM_CHANNEL  3   // GPIO14, pwmchip3/pwm0

#define PWM_SYSFS_BASE  "/sys/class/pwm/pwmchip"
#define NS_PER_SECOND   1000000000

// Per-motor state
typedef struct {
    int channel;
    int duty_ns;
    int duty_percent;
} motor_state_t;

static motor_state_t s_motors[PWM_NUM_MOTORS];
static int s_period_ns = 0;
static int s_frequency_hz = 0;
static bool s_initialized = false;

// Helper: write a string to a sysfs file
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

// export a single PWM channel
static int export_channel(int channel)
{
    char path[128];
    char val[16];

    // Skip if already exported
    snprintf(path, sizeof(path), "%s%d/pwm%d/enable", PWM_SYSFS_BASE, PWMCHIP, channel);
    if (access(path, F_OK) == 0) {
        return 0;
    }

    snprintf(path, sizeof(path), "%s%d/export", PWM_SYSFS_BASE, PWMCHIP);
    snprintf(val, sizeof(val), "%d", channel);
    if (write_sysfs(path, val) != 0) {
        fprintf(stderr, "PWM HAL: failed to export pwmchip%d channel %d\n", PWMCHIP, channel);
        return -1;
    }
    usleep(100000);
    return 0;
}

// set period for a channel
static int set_period(int channel, int period_ns)
{
    char path[128];
    char val[16];

    // Zero duty first (duty must be <= period)
    snprintf(path, sizeof(path), "%s%d/pwm%d/duty_cycle", PWM_SYSFS_BASE, PWMCHIP, channel);
    write_sysfs(path, "0");

    snprintf(path, sizeof(path), "%s%d/pwm%d/period", PWM_SYSFS_BASE, PWMCHIP, channel);
    snprintf(val, sizeof(val), "%d", period_ns);
    return write_sysfs(path, val);
}

// set duty cycle for a channel (in nanoseconds)
static int set_duty_ns(int channel, int duty_ns)
{
    char path[128];
    char val[16];
    snprintf(path, sizeof(path), "%s%d/pwm%d/duty_cycle", PWM_SYSFS_BASE, PWMCHIP, channel);
    snprintf(val, sizeof(val), "%d", duty_ns);
    return write_sysfs(path, val);
}

// enable/disable a channel
static int enable_channel(int channel, bool enable)
{
    char path[128];
    snprintf(path, sizeof(path), "%s%d/pwm%d/enable", PWM_SYSFS_BASE, PWMCHIP, channel);
    return write_sysfs(path, enable ? "1" : "0");
}

static int unexport_channel(int channel)
{
    char path[128];
    char val[16];
    snprintf(path, sizeof(path), "%s%d/unexport", PWM_SYSFS_BASE, PWMCHIP);
    snprintf(val, sizeof(val), "%d", channel);
    return write_sysfs(path, val);
}

int pwm_init(void)
{
    if (s_initialized) {
        fprintf(stderr, "PWM HAL: already initialized\n");
        return -1;
    }

    // Map logical channels to hardware channels
    s_motors[BTS_RPWM].channel = MOTOR1_CHANNEL;
    s_motors[BTS_LPWM].channel = MOTOR2_CHANNEL;
    
    // Export both channels
    for (int i = 0; i < PWM_NUM_MOTORS; i++) {
        if (export_channel(s_motors[i].channel) != 0) {
            return -1;
        }
        s_motors[i].duty_ns = 0;
        s_motors[i].duty_percent = 0;
    }

    // Default frequency: 1 kHz
    s_frequency_hz = 1000;
    s_period_ns = NS_PER_SECOND / s_frequency_hz;

    // Set period and zero duty on both channels
    for (int i = 0; i < PWM_NUM_MOTORS; i++) {
        if (set_period(s_motors[i].channel, s_period_ns) != 0) {return -1;}
        if (set_duty_ns(s_motors[i].channel, 0) != 0) {return -1;}
    }

    s_initialized = true;
    printf("PWM HAL: initialized (pwmchip%d, motor1=pwm%d/GPIO12, motor2=pwm%d/GPIO15, freq=%d Hz)\n", PWMCHIP, MOTOR1_CHANNEL, MOTOR2_CHANNEL, s_frequency_hz);
    return 0;
}

int pwm_set_frequency(int frequency_hz)
{
    if (!s_initialized) {
        return -1;
    }
    if (frequency_hz < 50 || frequency_hz > 20000) {
        fprintf(stderr, "PWM HAL: frequency %d Hz out of range (50-20000)\n", frequency_hz);
        return -1;
    }

    s_frequency_hz = frequency_hz;
    s_period_ns = NS_PER_SECOND / frequency_hz;

    // Update both channels
    for (int i = 0; i < PWM_NUM_MOTORS; i++) {
        s_motors[i].duty_ns = s_period_ns * s_motors[i].duty_percent / 100;
        if (set_period(s_motors[i].channel, s_period_ns) != 0) {return -1;}
        if (set_duty_ns(s_motors[i].channel, s_motors[i].duty_ns) != 0) {return -1;}
    }

    printf("PWM HAL: frequency set to %d Hz (period = %d ns)\n",
           frequency_hz, s_period_ns);
    return 0;
}

int pwm_set_duty_cycle(int motor, int duty_percent)
{
    if (!s_initialized || motor < 0 || motor >= PWM_NUM_MOTORS) {
        return -1;
    }
    if (duty_percent < 0 || duty_percent > 100) {
        fprintf(stderr, "PWM HAL: duty cycle %d%% out of range (0-100)\n", duty_percent);
        return -1;
    }

    s_motors[motor].duty_percent = duty_percent;
    s_motors[motor].duty_ns = s_period_ns * duty_percent / 100;

    if (set_duty_ns(s_motors[motor].channel, s_motors[motor].duty_ns) != 0) {return -1;}

    printf("PWM HAL: motor %d duty cycle set to %d%%\n", motor + 1, duty_percent);
    return 0;
}

int pwm_enable(int motor, bool enable)
{
    if (!s_initialized || motor < 0 || motor >= PWM_NUM_MOTORS) {return -1;}
    if (enable_channel(s_motors[motor].channel, enable) != 0) {return -1;}

    printf("PWM HAL: motor %d %s\n", motor + 1, enable ? "enabled" : "disabled");
    return 0;
}


void pwm_cleanup(void)
{
    if (!s_initialized) {return;}
    
    for (int i = 0; i < PWM_NUM_MOTORS; i++) {
        enable_channel(s_motors[i].channel, false);
        set_duty_ns(s_motors[i].channel, 0);
        unexport_channel(s_motors[i].channel);
    }
    
    s_initialized = false;
    printf("PWM HAL: cleaned up (both motors)\n");
}

int pwm_get_frequency(void){return s_frequency_hz;}

int pwm_get_duty_cycle(int motor)
{
    if (motor < 0 || motor >= PWM_NUM_MOTORS) {return -1;}
    return s_motors[motor].duty_percent;
}