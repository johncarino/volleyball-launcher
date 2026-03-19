#include "stepper.h"
#include <unistd.h>
#include <stdio.h>


// GPIO pins
#define STEP_PIN 16
#define DIR_PIN  18

int stepper_init(Stepper *motor, const char *chipname, unsigned int step_pin, unsigned int dir_pin) {
    struct gpiod_line_settings *line_settings = NULL;
    struct gpiod_line_config *line_config = NULL;
    struct gpiod_request_config *request_config = NULL;
    unsigned int offsets[2] = { step_pin, dir_pin };
    char chip_path[128];

    motor->step_pin = step_pin;
    motor->dir_pin = dir_pin;
    motor->request = NULL;

    if (chipname[0] == '/') {
        motor->chip = gpiod_chip_open(chipname);
    } else {
        int written = snprintf(chip_path, sizeof(chip_path), "/dev/%s", chipname);
        if (written < 0 || (size_t)written >= sizeof(chip_path)) {
            perror("Invalid GPIO chip name");
            return -1;
        }
        motor->chip = gpiod_chip_open(chip_path);
    }

    if (!motor->chip) {
        perror("Failed to open GPIO chip");
        return -1;
    }

    line_settings = gpiod_line_settings_new();
    line_config = gpiod_line_config_new();
    request_config = gpiod_request_config_new();
    if (!line_settings || !line_config || !request_config) {
        perror("Failed to create GPIO request configuration");
        goto fail;
    }

    if (gpiod_line_settings_set_direction(line_settings, GPIOD_LINE_DIRECTION_OUTPUT) < 0 ||
        gpiod_line_settings_set_output_value(line_settings, GPIOD_LINE_VALUE_INACTIVE) < 0) {
        perror("Failed to configure GPIO line settings");
        goto fail;
    }

    if (gpiod_line_config_add_line_settings(line_config, offsets, 2, line_settings) < 0) {
        perror("Failed to add GPIO line settings");
        goto fail;
    }

    gpiod_request_config_set_consumer(request_config, "stepper");
    motor->request = gpiod_chip_request_lines(motor->chip, request_config, line_config);
    if (!motor->request) {
        perror("Failed to request GPIO lines as output");
        goto fail;
    }

    gpiod_request_config_free(request_config);
    gpiod_line_config_free(line_config);
    gpiod_line_settings_free(line_settings);

    return 0;

fail:
    gpiod_request_config_free(request_config);
    gpiod_line_config_free(line_config);
    gpiod_line_settings_free(line_settings);
    stepper_cleanup(motor);
    return -1;
}

void stepper_set_direction(Stepper *motor, int direction) {
    gpiod_line_request_set_value(
        motor->request,
        motor->dir_pin,
        direction ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
}

void stepper_step(Stepper *motor, int steps, int delay_us) {
    for (int i = 0; i < steps; i++) {
        gpiod_line_request_set_value(motor->request, motor->step_pin, GPIOD_LINE_VALUE_ACTIVE);
        usleep(delay_us);
        gpiod_line_request_set_value(motor->request, motor->step_pin, GPIOD_LINE_VALUE_INACTIVE);
        usleep(delay_us);
    }
}

void stepper_cleanup(Stepper *motor) {
    if (motor->request) {
        gpiod_line_request_release(motor->request);
        motor->request = NULL;
    }

    if (motor->chip) {
        gpiod_chip_close(motor->chip);
        motor->chip = NULL;
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