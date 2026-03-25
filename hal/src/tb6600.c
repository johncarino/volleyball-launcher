#include "hal/tb6600.h"
#include <stdio.h>
#include <unistd.h> // usleep

// GPIO pin assignments
#define STEP_PIN 4 //GPIO 9 offset 4 pin21
#define DIR_PIN  10 //GPIO 24 offset 10 pin18
#define EN_PIN   7 //GPIO 23 offset 7 pin16
//#define CHIPNAME "/dev/gpiochip0"

static const unsigned int step_offset = STEP_PIN;
static const unsigned int dir_offset = DIR_PIN;
static const unsigned int en_offset = EN_PIN;

int tb6600_init(tb6600_t *motor, int use_enable)
{
    const char *chipname = "/dev/gpiochip0";

    struct gpiod_line_settings *line_settings = NULL;
    struct gpiod_line_config *line_config = NULL;
    struct gpiod_request_config *request_config = NULL;

    motor->chip = NULL;
    motor->request = NULL;
    motor->step_pin = step_offset;
    motor->dir_pin = dir_offset;
    motor->en_pin = en_offset;
    motor->use_enable = use_enable;

    // Open chip
    motor->chip = gpiod_chip_open(chipname);

    if (!motor->chip) {
        perror("Failed to open GPIO chip");
        return -1;
    }

    line_settings = gpiod_line_settings_new();
    line_config = gpiod_line_config_new();
    request_config = gpiod_request_config_new();
    if (!line_settings || !line_config || !request_config) {
        perror("Failed to create GPIO config");
        goto fail;
    }

    if (gpiod_line_settings_set_direction(line_settings, GPIOD_LINE_DIRECTION_OUTPUT) < 0 ||
        gpiod_line_settings_set_output_value(line_settings, GPIOD_LINE_VALUE_INACTIVE) < 0) {
        perror("Failed to set line settings");
        goto fail;
    }

    if (gpiod_line_config_add_line_settings(line_config, &step_offset, 1, line_settings) < 0 ||
        gpiod_line_config_add_line_settings(line_config, &dir_offset, 1, line_settings) < 0 ||
        (use_enable && gpiod_line_config_add_line_settings(line_config, &en_offset, 1, line_settings) < 0)) {
        perror("Failed to add line settings");
        goto fail;
    }

    gpiod_request_config_set_consumer(request_config, "tb6600_test");
    motor->request = gpiod_chip_request_lines(motor->chip, request_config, line_config);
    if (!motor->request) {
        perror("Failed to request lines");
        goto fail;
    }

    gpiod_request_config_free(request_config);
    gpiod_line_config_free(line_config);
    gpiod_line_settings_free(line_settings);

    return 0;

fail:
    if (request_config) {
        gpiod_request_config_free(request_config);
    }
    if (line_config) {
        gpiod_line_config_free(line_config);
    }
    if (line_settings) {
        gpiod_line_settings_free(line_settings);
    }
    tb6600_close(motor);
    return -1;
}

void tb6600_set_direction(tb6600_t *motor, int dir)
{
    if (gpiod_line_request_set_value(motor->request,
            dir_offset,
            dir ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE) < 0) {
        perror("dir set failed");
    }
}

void tb6600_enable(tb6600_t *motor, int enable)
{
    if (!motor->use_enable) return;

    // TB6600: LOW = enabled
    if (gpiod_line_request_set_value(motor->request, en_offset, enable ? GPIOD_LINE_VALUE_INACTIVE : GPIOD_LINE_VALUE_ACTIVE) < 0) {
        perror("enable set failed");
    }
}

void tb6600_step(tb6600_t *motor, int steps, int delay_us)
{
    printf("TB6600: stepping %d steps with %d us delay per half-cycle\n", steps, delay_us);

    for (int i = 0; i < steps; i++) {
        if (gpiod_line_request_set_value(motor->request, step_offset, GPIOD_LINE_VALUE_ACTIVE) < 0) {
            perror("step high failed");
            return;
        }

        usleep(delay_us);

        if (gpiod_line_request_set_value(motor->request, step_offset, GPIOD_LINE_VALUE_INACTIVE) < 0) {
            perror("step low failed");
            return;
        }

        usleep(delay_us);
    }

    printf("TB6600: stepping complete (%d steps done)\n", steps);
}

void tb6600_step_accel(tb6600_t *motor, int steps, int start_delay_us,
                       int end_delay_us, int accel_steps)
{
    if (accel_steps > steps / 2) {
        accel_steps = steps / 2;
    }

    printf("TB6600: stepping %d steps (accel %d→%d us over %d steps)\n",
           steps, start_delay_us, end_delay_us, accel_steps);

    for (int i = 0; i < steps; i++) {
        // Compute current delay: ramp down during accel, ramp up during decel
        int delay;
        if (i < accel_steps) {
            // Accelerating: linearly decrease delay from start to end
            delay = start_delay_us - (start_delay_us - end_delay_us) * i / accel_steps;
        } else if (i >= steps - accel_steps) {
            // Decelerating: linearly increase delay from end back to start
            int remaining = steps - 1 - i;
            delay = start_delay_us - (start_delay_us - end_delay_us) * remaining / accel_steps;
        } else {
            // Cruising at target speed
            delay = end_delay_us;
        }

        if (gpiod_line_request_set_value(motor->request, step_offset, GPIOD_LINE_VALUE_ACTIVE) < 0) {
            perror("step high failed");
            return;
        }
        usleep(delay);

        if (gpiod_line_request_set_value(motor->request, step_offset, GPIOD_LINE_VALUE_INACTIVE) < 0) {
            perror("step low failed");
            return;
        }
        usleep(delay);
    }

    printf("TB6600: stepping complete (%d steps done)\n", steps);
}

void tb6600_close(tb6600_t *motor)
{
    if (motor->request) {
        gpiod_line_request_release(motor->request);
        motor->request = NULL;
    }

    if (motor->chip) {
        gpiod_chip_close(motor->chip);
        motor->chip = NULL;
    }
}
