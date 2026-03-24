#ifndef TB6600_H
#define TB6600_H

#include <gpiod.h>

typedef struct {
    struct gpiod_chip *chip;
    struct gpiod_line_request *request;

    unsigned int step_pin;
    unsigned int dir_pin;
    unsigned int en_pin;

    int use_enable;
} tb6600_t;

#define TB6600_CHIP "/dev/gpiochip0"

int tb6600_init(tb6600_t *motor, int use_enable);
void tb6600_set_direction(tb6600_t *motor, int dir);
void tb6600_enable(tb6600_t *motor, int enable);
void tb6600_step(tb6600_t *motor, int steps, int delay_us);
void tb6600_close(tb6600_t *motor);

#endif // TB6600_H
