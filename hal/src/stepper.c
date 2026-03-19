#include "stepper.h"
#include <unistd.h>
#include <stdio.h>

// gcc stepper.c -lgpiod -o stepper
// sudo ./stepper

// GPIO pins
#define STEP_PIN 16
#define DIR_PIN  18

int stepper_init(Stepper *motor, const char *chipname, unsigned int step_pin, unsigned int dir_pin) {

    motor->step_pin = step_pin;
    motor->dir_pin = dir_pin;

    motor->chip = gpiod_chip_open_by_name(chipname);
    if (!motor->chip) {
        perror("Failed to open GPIO chip");
        return -1;
    }

    motor->step_line = gpiod_chip_get_line(motor->chip, step_pin);
    motor->dir_line  = gpiod_chip_get_line(motor->chip, dir_pin);

    if (!motor->step_line || !motor->dir_line) {
        perror("Failed to get GPIO lines");
        return -1;
    }

    if (gpiod_line_request_output(motor->step_line, "stepper", 0) < 0 ||
        gpiod_line_request_output(motor->dir_line, "stepper", 0) < 0) {
        perror("Failed to request GPIO lines as output");
        return -1;
    }

    return 0;
}

void stepper_set_direction(Stepper *motor, int direction) {
    gpiod_line_set_value(motor->dir_line, direction ? 1 : 0);
}

void stepper_step(Stepper *motor, int steps, int delay_us) {
    for (int i = 0; i < steps; i++) {
        gpiod_line_set_value(motor->step_line, 1);
        usleep(delay_us);
        gpiod_line_set_value(motor->step_line, 0);
        usleep(delay_us);
    }
}

void stepper_cleanup(Stepper *motor) {
    if (motor->chip) {
        gpiod_chip_close(motor->chip);
    }
}

// Test program
int main() {
    Stepper motor;

    if (stepper_init(&motor, "gpiochip0", STEP_PIN, DIR_PIN) < 0) {
        return -1;
    }

    printf("Stepper initialized\n");

    // Forward rotation
    printf("Rotating forward...\n");
    stepper_set_direction(&motor, 1);
    stepper_step(&motor, 200, 1000); // 200 steps with 1ms delay

    usleep(500000); // pause for 0.5 seconds

    // Reverse rotation
    printf("Rotating reverse...\n");
    stepper_set_direction(&motor, 0);
    stepper_step(&motor, 200, 1000); // 200 steps with 1ms delay

    stepper_cleanup(&motor);
    printf("Done\n");

    return 0;
}