#include "buzzer.h"
#include <stdio.h>
#include <unistd.h>
#include <gpiod.h>

/* ---- Buzzer output (chip 1, offset 33) ---- */
#define BUZZER_GPIO_CHIP_PATH "/dev/gpiochip1"
#define BUZZER_GPIO_OFFSET 33

static struct gpiod_chip *buzzer_chip = NULL;
static struct gpiod_line_request *buzzer_request = NULL;
static int buzzer_initialized = 0;

static int buzzer_init(void) {
    if (buzzer_initialized) {
        return 0;
    }

    buzzer_chip = gpiod_chip_open(BUZZER_GPIO_CHIP_PATH);
    if (!buzzer_chip) {
        perror("Buzzer: failed to open " BUZZER_GPIO_CHIP_PATH);
        return -1;
    }

    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    if (!settings) {
        perror("Buzzer: failed to allocate line settings");
        gpiod_chip_close(buzzer_chip);
        buzzer_chip = NULL;
        return -1;
    }
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);

    struct gpiod_line_config *line_cfg = gpiod_line_config_new();
    if (!line_cfg) {
        perror("Buzzer: failed to allocate line config");
        gpiod_line_settings_free(settings);
        gpiod_chip_close(buzzer_chip);
        buzzer_chip = NULL;
        return -1;
    }

    unsigned int offset = BUZZER_GPIO_OFFSET;
    if (gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings) != 0) {
        perror("Buzzer: failed to add line settings");
        gpiod_line_config_free(line_cfg);
        gpiod_line_settings_free(settings);
        gpiod_chip_close(buzzer_chip);
        buzzer_chip = NULL;
        return -1;
    }

    struct gpiod_request_config *req_cfg = gpiod_request_config_new();
    if (!req_cfg) {
        perror("Buzzer: failed to allocate request config");
        gpiod_line_config_free(line_cfg);
        gpiod_line_settings_free(settings);
        gpiod_chip_close(buzzer_chip);
        buzzer_chip = NULL;
        return -1;
    }
    gpiod_request_config_set_consumer(req_cfg, "volleyball-buzzer");

    buzzer_request = gpiod_chip_request_lines(buzzer_chip, req_cfg, line_cfg);

    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);

    if (!buzzer_request) {
        perror("Buzzer: failed to request line as output");
        gpiod_chip_close(buzzer_chip);
        buzzer_chip = NULL;
        return -1;
    }

    buzzer_initialized = 1;
    return 0;
}

void buzzer_cleanup(void) {
    if (buzzer_request) {
        gpiod_line_request_set_value(buzzer_request, BUZZER_GPIO_OFFSET, GPIOD_LINE_VALUE_INACTIVE);
        gpiod_line_request_release(buzzer_request);
        buzzer_request = NULL;
    }
    if (buzzer_chip) {
        gpiod_chip_close(buzzer_chip);
        buzzer_chip = NULL;
    }
    buzzer_initialized = 0;
}


// Active buzzer: has its own internal oscillator, so it just needs the GPIO
// held HIGH for the duration to sound, then LOW to stop.
void buzzer_tone(int duration_ms) {
    if (buzzer_init() != 0) {
        return;
    }

    gpiod_line_request_set_value(buzzer_request, BUZZER_GPIO_OFFSET, GPIOD_LINE_VALUE_ACTIVE);
    usleep((long)duration_ms * 1000);
    gpiod_line_request_set_value(buzzer_request, BUZZER_GPIO_OFFSET, GPIOD_LINE_VALUE_INACTIVE);
}