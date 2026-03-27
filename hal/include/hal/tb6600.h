// TB6600 stepper driver HAL using libgpiod character-device GPIO.
//
// The driver exposes STEP, DIR, and optional EN (enable) control pins.
// STEP pulses advance the motor, DIR sets rotation direction, and EN can
// electrically enable/disable the TB6600 output stage.

#ifndef TB6600_H
#define TB6600_H

#include <gpiod.h>

// Runtime state for a TB6600-controlled stepper motor.
typedef struct {
    // Open GPIO chip handle (e.g. /dev/gpiochip0).
    struct gpiod_chip *chip;
    // Multi-line request handle for STEP/DIR/(optional) EN lines.
    struct gpiod_line_request *request;

    // GPIO offsets within the selected chip.
    unsigned int step_pin;
    unsigned int dir_pin;
    unsigned int en_pin;

    // Non-zero when EN control is used, 0 when EN is not wired/managed.
    int use_enable;
} tb6600_t;

// Default GPIO chip device used by the platform.
#define TB6600_CHIP "/dev/gpiochip0"

// Initialize the TB6600 GPIO lines and optionally include EN control.
// motor: caller-provided state structure to initialize
// use_enable: non-zero to request/use EN pin, 0 to ignore EN
// Returns 0 on success, -1 on failure.
int tb6600_init(tb6600_t *motor, int use_enable);

// Set stepper direction.
// dir: 0 or 1 (mapped to TB6600 DIR logic level)
void tb6600_set_direction(tb6600_t *motor, int dir);

// Enable or disable the TB6600 output stage (if EN is configured).
// enable: non-zero enables driver output, 0 disables output
void tb6600_enable(tb6600_t *motor, int enable);

// Generate STEP pulses to move the motor.
// steps: number of step pulses to output
// delay_us: microsecond delay between pulse edges (speed control)
void tb6600_step(tb6600_t *motor, int steps, int delay_us);

// Release GPIO resources associated with the TB6600 instance.
void tb6600_close(tb6600_t *motor);

#endif // TB6600_H
