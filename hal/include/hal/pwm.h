// PWM HAL module for controlling DC motor speed via the ZS-X11H driver board.
// Uses Linux sysfs PWM interface on BeagleY-AI.
//
// Wiring: P (signal input) and G (ground) soldered to the driver board.
//         Short-circuit pads enabled for external PWM input.
//         PWM output on GPIO12 (pin 32, PWM0 channel B).

#ifndef HAL_PWM_H
#define HAL_PWM_H

#include <stdbool.h>

// Initialize PWM for motor speed control.
// pwmchip: the PWM chip number (e.g., 0 for /sys/class/pwm/pwmchip0)
// channel: the PWM channel number within the chip
// Returns 0 on success, -1 on failure.
int pwm_init(int pwmchip, int channel);

// Set the PWM frequency in Hz (e.g., 1000 for 1 kHz).
// Valid range for the driver board: 50 - 20000 Hz.
// Returns 0 on success, -1 on failure.
int pwm_set_frequency(int frequency_hz);

// Set the duty cycle as a percentage (0 - 100).
// 0 = motor stopped, 100 = full speed.
// Returns 0 on success, -1 on failure.
int pwm_set_duty_cycle(int duty_percent);

// Enable or disable the PWM output (starts/stops the motor).
// Returns 0 on success, -1 on failure.
int pwm_enable(bool enable);

// Get the current duty cycle percentage (0-100).
int pwm_get_duty_cycle(void);

// Get the current frequency in Hz.
int pwm_get_frequency(void);

// Clean up: disable PWM, unexport channel.
void pwm_cleanup(void);

#endif // HAL_PWM_H
