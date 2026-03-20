#include "hal/tb6600.h"
#include <stdio.h>
#include <unistd.h> // usleep

// Indices into the requested line array
#define STEP_IDX 0
#define DIR_IDX  1
#define EN_IDX   2

// GPIO pin assignments
#define STEP_PIN 16
#define DIR_PIN  18
#define EN_PIN   22

int tb6600_init(tb6600_t *motor, const char *chipname, int use_enable)
{
    struct gpiod_line_settings *line_settings = NULL;
    struct gpiod_line_config *line_config = NULL;
    struct gpiod_request_config *request_config = NULL;
    unsigned int offsets[3];
    size_t num_offsets;
    char chip_path[128];

    motor->chip = NULL;
    motor->request = NULL;
    motor->step_pin = STEP_PIN;
    motor->dir_pin = DIR_PIN;
    motor->en_pin = EN_PIN;
    motor->use_enable = use_enable;

    // Build offsets array properly
    offsets[STEP_IDX] = STEP_PIN;
    offsets[DIR_IDX]  = DIR_PIN;
    if (use_enable) {
        offsets[EN_IDX] = EN_PIN;
        num_offsets = 3;
    } else {
        num_offsets = 2;
    }

    // Open chip
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
        perror("Failed to create GPIO config");
        goto fail;
    }

    if (gpiod_line_settings_set_direction(line_settings, GPIOD_LINE_DIRECTION_OUTPUT) < 0 ||
        gpiod_line_settings_set_output_value(line_settings, GPIOD_LINE_VALUE_INACTIVE) < 0) {
        perror("Failed to set line settings");
        goto fail;
    }

    if (gpiod_line_config_add_line_settings(line_config, offsets, num_offsets, line_settings) < 0) {
        perror("Failed to add line settings");
        goto fail;
    }

    gpiod_request_config_set_consumer(request_config, "tb6600");
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
    gpiod_request_config_free(request_config);
    gpiod_line_config_free(line_config);
    gpiod_line_settings_free(line_settings);
    tb6600_close(motor);
    return -1;
}

void tb6600_set_direction(tb6600_t *motor, int dir)
{
    if (gpiod_line_request_set_value(motor->request,
            DIR_IDX,
            dir ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE) < 0) {
        perror("dir set failed");
    }
}

void tb6600_enable(tb6600_t *motor, int enable)
{
    if (!motor->use_enable) return;

    // TB6600: LOW = enabled
    if (gpiod_line_request_set_value(motor->request, EN_IDX, enable ? GPIOD_LINE_VALUE_INACTIVE : GPIOD_LINE_VALUE_ACTIVE) < 0) {
        perror("enable set failed");
    }
}

void tb6600_step(tb6600_t *motor, int steps, int delay_us)
{
    for (int i = 0; i < steps; i++) {

        if (gpiod_line_request_set_value(motor->request, STEP_IDX, GPIOD_LINE_VALUE_ACTIVE) < 0) {
            perror("step high failed");
        }

        usleep(delay_us);

        if (gpiod_line_request_set_value(motor->request, STEP_IDX, GPIOD_LINE_VALUE_INACTIVE) < 0) {
            perror("step low failed");
        }
        usleep(delay_us);
    }
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
