// PWM HAL module for controlling two DC motors via ZS-X11H driver boards.
// Uses Linux sysfs PWM interface on BeagleY-AI (EPWM0 / pwmchip3).
//
// Physical wiring:
//   Motor 1: P + G soldered → GPIO12 (pin 32) = pwmchip3/pwm1 (channel B)
//   Motor 2: P + G soldered → GPIO15 (pin 10) = pwmchip3/pwm0 (channel A)
//   Both driver boards have PWM short-circuit pads enabled.
//   On-board potentiometers turned to minimum.

#ifndef HAL_PWM_H
#define HAL_PWM_H

#include <stdbool.h>

// Motor identifiers
#define PWM_MOTOR_1  0   // GPIO12, pwmchip3/pwm1
#define PWM_MOTOR_2  1   // GPIO15, pwmchip3/pwm0

// USED IN BTS7960.C
#define BTS_RPWM  2   // GPIO5, pwmchip3/pwm1
#define BTS_LPWM  3   // GPIO14, pwmchip3/pwm0

#define PWM_NUM_MOTORS 4

// Initialize both PWM channels for motor speed control.
// Returns 0 on success, -1 on failure.
int pwm_init(void);

// Set the PWM frequency in Hz (applies to BOTH motors — shared timer).
// Valid range for the driver board: 50 - 20000 Hz.
// Returns 0 on success, -1 on failure.
int pwm_set_frequency(int frequency_hz);

// Set the duty cycle for a specific motor as a percentage (0 - 100).
// 0 = motor stopped, 100 = full speed.
// motor: PWM_MOTOR_1 or PWM_MOTOR_2
// Returns 0 on success, -1 on failure.
int pwm_set_duty_cycle(int motor, int duty_percent);

// Enable or disable PWM output for a specific motor.
// motor: PWM_MOTOR_1 or PWM_MOTOR_2
// Returns 0 on success, -1 on failure.
int pwm_enable(int motor, bool enable);

// Get the current duty cycle percentage (0-100) for a motor.
int pwm_get_duty_cycle(int motor);

// Get the current frequency in Hz (shared by both motors).
int pwm_get_frequency(void);

// Clean up: disable both PWM channels, unexport.
void pwm_cleanup(void);

#endif // HAL_PWM_H
