#include "hal/stepper.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>

// Helper: request a single GPIO line as output with initial value 0
static struct gpiod_line_request *request_output_line(struct gpiod_chip *chip,
                                                      unsigned int pin,
                                                      const char *consumer)
{
    struct gpiod_line_settings   *settings    = NULL;
    struct gpiod_line_config     *line_cfg    = NULL;
    struct gpiod_request_config  *req_cfg     = NULL;
    struct gpiod_line_request    *request     = NULL;

    settings = gpiod_line_settings_new();
    if (!settings) goto cleanup;

    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);

    line_cfg = gpiod_line_config_new();
    if (!line_cfg) goto cleanup;

    if (gpiod_line_config_add_line_settings(line_cfg, &pin, 1, settings) < 0)
        goto cleanup;

    req_cfg = gpiod_request_config_new();
    if (!req_cfg) goto cleanup;

    gpiod_request_config_set_consumer(req_cfg, consumer);

    request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);

cleanup:
    if (req_cfg)  gpiod_request_config_free(req_cfg);
    if (line_cfg) gpiod_line_config_free(line_cfg);
    if (settings) gpiod_line_settings_free(settings);
    return request;   // NULL on error
}

int stepper_init(Stepper *motor, const char *chipname, unsigned int step_pin, unsigned int dir_pin) {

    motor->step_pin = step_pin;
    motor->dir_pin  = dir_pin;
    motor->step_request = NULL;
    motor->dir_request  = NULL;

    // v2: open by path, e.g. "/dev/gpiochip0"  or bare name "gpiochip0"
    char path[64];
    if (chipname[0] == '/') {
        snprintf(path, sizeof(path), "%s", chipname);
    } else {
        snprintf(path, sizeof(path), "/dev/%s", chipname);
    }

    motor->chip = gpiod_chip_open(path);
    if (!motor->chip) {
        perror("Failed to open GPIO chip");
        return -1;
    }

    motor->step_request = request_output_line(motor->chip, step_pin, "stepper-step");
    if (!motor->step_request) {
        perror("Failed to request step GPIO line");
        stepper_cleanup(motor);
        return -1;
    }

    motor->dir_request = request_output_line(motor->chip, dir_pin, "stepper-dir");
    if (!motor->dir_request) {
        perror("Failed to request dir GPIO line");
        stepper_cleanup(motor);
        return -1;
    }

    return 0;
}

void stepper_set_direction(Stepper *motor, int direction) {
    gpiod_line_request_set_value(motor->dir_request, motor->dir_pin,
                                 direction ? GPIOD_LINE_VALUE_ACTIVE
                                           : GPIOD_LINE_VALUE_INACTIVE);
}

void stepper_step(Stepper *motor, int steps, int delay_us) {
    for (int i = 0; i < steps; i++) {
        gpiod_line_request_set_value(motor->step_request, motor->step_pin,
                                     GPIOD_LINE_VALUE_ACTIVE);
        usleep(delay_us);
        gpiod_line_request_set_value(motor->step_request, motor->step_pin,
                                     GPIOD_LINE_VALUE_INACTIVE);
        usleep(delay_us);
    }
}

void stepper_cleanup(Stepper *motor) {
    if (motor->step_request) {
        gpiod_line_request_release(motor->step_request);
        motor->step_request = NULL;
    }
    if (motor->dir_request) {
        gpiod_line_request_release(motor->dir_request);
        motor->dir_request = NULL;
    }
    if (motor->chip) {
        gpiod_chip_close(motor->chip);
        motor->chip = NULL;
    }
}