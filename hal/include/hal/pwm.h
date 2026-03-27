// PWM HAL implementation using Linux sysfs for BeagleY-AI.
// Both on EPWM0 (pwmchip3): channel A (pwm0) and channel B (pwm1).
//
// Physical wiring:
//   Motor 1: GPIO12 (pin 32) → pwmchip3/pwm1 (channel B)
//   Motor 2: GPIO15 (pin 10) → pwmchip3/pwm0 (channel A)

#ifndef HAL_PWM_H
#define HAL_PWM_H

#include <stdbool.h>

// BTS7960 compatibility aliases for forward/reverse PWM inputs.
#define BTS_RPWM  0   // GPIO12, pwmchip3/pwm1
#define BTS_LPWM  1   // GPIO15, pwmchip3/pwm0

#define PWM_NUM_MOTORS 2

// Initialize both PWM channels for motor speed control.
// Returns 0 on success, -1 on failure.
int pwm_init(void);

// Set the PWM frequency in Hz (applies to BOTH motors — shared timer).
// Valid range for the driver board: 50 - 20000 Hz.
// Returns 0 on success, -1 on failure.
int pwm_set_frequency(int frequency_hz);

// Set the duty cycle for a specific motor as a percentage (0 - 100).
// 0 = motor stopped, 100 = full speed.
// motor: channel index (0 or 1), e.g. BTS_RPWM or BTS_LPWM
// Returns 0 on success, -1 on failure.
int pwm_set_duty_cycle(int motor, int duty_percent);

// Enable or disable PWM output for a specific motor.
// motor: channel index (0 or 1), e.g. BTS_RPWM or BTS_LPWM
// Returns 0 on success, -1 on failure.
int pwm_enable(int motor, bool enable);

// Get the current duty cycle percentage (0-100) for a motor.
int pwm_get_duty_cycle(int motor);

// Get the current frequency in Hz (shared by both motors).
int pwm_get_frequency(void);

// Clean up: disable both PWM channels, unexport.
void pwm_cleanup(void);

#endif // HAL_PWM_H
