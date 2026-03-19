#ifndef STEPPER_H
#define STEPPER_H

#include <gpiod.h>

typedef struct {
    struct gpiod_chip *chip;
    struct gpiod_line_request *request;

    unsigned int step_pin;
    unsigned int dir_pin;
    unsigned int en_pin;

    int use_enable;
} stepper_t;

int stepper_init(stepper_t *motor,
                 const char *chipname,
                 unsigned int step_pin,
                 unsigned int dir_pin,
                 unsigned int en_pin,
                 int use_enable);

void stepper_set_direction(stepper_t *motor, int dir);
void stepper_enable(stepper_t *motor, int enable);
void stepper_step(stepper_t *motor, int steps, int delay_us);
void stepper_close(stepper_t *motor);

#endif