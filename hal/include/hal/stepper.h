#ifndef STEPPER_H
#define STEPPER_H

#include <gpiod.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    struct gpiod_chip *chip;
    struct gpiod_line_request *request;
    unsigned int step_pin;
    unsigned int dir_pin;
} Stepper;

int stepper_init(Stepper *motor, const char *chipname, unsigned int step_pin, unsigned int dir_pin);

void stepper_set_direction(Stepper *motor, int direction);

void stepper_step(Stepper *motor, int steps, int delay_us);

void stepper_cleanup(Stepper *motor);

#endif