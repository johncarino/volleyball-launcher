#define _POSIX_C_SOURCE 199309L

#include "hcsr04.h"
#include <gpiod.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>

#define HCSR04_CHIP_PATH        "/dev/gpiochip0"
#define HCSR04_TRIG_OFFSET      2
#define HCSR04_ECHO_OFFSET      3
#define HCSR04_ECHO_TIMEOUT_US  30000U
#define HCSR04_CM_PER_US_ROUNDTRIP 0.01715f /* 0.0343 / 2 */
#define HCSR04_TRIG_PULSE_US    10U
#define BALL_PRESENT_MAX_DISTANCE_CM 5.0f   /* ultrasonic sensing distance in cm*/

static struct gpiod_chip *chip;
static struct gpiod_line_request *trig_req;
static struct gpiod_line_request *echo_req;
static int ready = 0;

static void trig_write(bool level)
{
    gpiod_line_request_set_value(trig_req, HCSR04_TRIG_OFFSET,
        level ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
}

static bool echo_read(void)
{
    return gpiod_line_request_get_value(echo_req, HCSR04_ECHO_OFFSET)
        == GPIOD_LINE_VALUE_ACTIVE;
}

static void delay_us(uint32_t us)
{
    struct timespec ts = {
        .tv_sec = us / 1000000U,
        .tv_nsec = (long)(us % 1000000U) * 1000L
    };
    nanosleep(&ts, NULL);
}

static uint32_t micros(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000000UL + ts.tv_nsec / 1000UL);
}

static struct gpiod_line_request *request_line(struct gpiod_chip *c,
    unsigned int offset, enum gpiod_line_direction direction, const char *consumer)
{
    struct gpiod_line_request *request = NULL;
    struct gpiod_request_config *req_cfg = NULL;
    struct gpiod_line_config *line_cfg = NULL;
    struct gpiod_line_settings *settings = NULL;

    settings = gpiod_line_settings_new();
    if (!settings) goto done;
    gpiod_line_settings_set_direction(settings, direction);

    line_cfg = gpiod_line_config_new();
    if (!line_cfg) goto done;
    if (gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings) < 0)
        goto done;

    req_cfg = gpiod_request_config_new();
    if (!req_cfg) goto done;
    gpiod_request_config_set_consumer(req_cfg, consumer);

    request = gpiod_chip_request_lines(c, req_cfg, line_cfg);

done:
    if (req_cfg) gpiod_request_config_free(req_cfg);
    if (line_cfg) gpiod_line_config_free(line_cfg);
    if (settings) gpiod_line_settings_free(settings);
    return request;
}

int hcsr04_init(void)
{
    chip = gpiod_chip_open(HCSR04_CHIP_PATH);
    if (!chip) {
        perror("hcsr04: gpiod_chip_open failed");
        return -1;
    }

    trig_req = request_line(chip, HCSR04_TRIG_OFFSET,
        GPIOD_LINE_DIRECTION_OUTPUT, "hcsr04-trig");
    if (!trig_req) {
        perror("hcsr04: failed to request TRIG line");
        return -1;
    }

    echo_req = request_line(chip, HCSR04_ECHO_OFFSET,
        GPIOD_LINE_DIRECTION_INPUT, "hcsr04-echo");
    if (!echo_req) {
        perror("hcsr04: failed to request ECHO line");
        return -1;
    }

    trig_write(false);
    ready = 1;
    return 0;
}

void hcsr04_cleanup(void)
{
    if (trig_req) {
        trig_write(false); /* leave TRIG in a safe, known state */
        gpiod_line_request_release(trig_req);
        trig_req = NULL;
    }
    if (echo_req) {
        gpiod_line_request_release(echo_req);
        echo_req = NULL;
    }
    if (chip) {
        gpiod_chip_close(chip);
        chip = NULL;
    }
    ready = 0;
}

static bool measure_raw(uint32_t *echo_duration_us)
{
    trig_write(false);
    delay_us(2);
    trig_write(true);
    delay_us(HCSR04_TRIG_PULSE_US);
    trig_write(false);

    uint32_t wait_start = micros();
    while (!echo_read()) {
        if ((micros() - wait_start) > HCSR04_ECHO_TIMEOUT_US) {
            return false;
        }
    }

    uint32_t pulse_start = micros();
    while (echo_read()) {
        if ((micros() - pulse_start) > HCSR04_ECHO_TIMEOUT_US) {
            return false;
        }
    }
    uint32_t pulse_end = micros();

    *echo_duration_us = pulse_end - pulse_start;
    return true;
}

float hcsr04_get_distance_cm(void)
{
    uint32_t duration_us;

    if (!ready) {
        fprintf(stderr, "hcsr04: not initialized\n");
        return -1.0f;
    }

    if (!measure_raw(&duration_us)) {
        return -1.0f;
    }

    return (float)duration_us * HCSR04_CM_PER_US_ROUNDTRIP;
}

bool hcsr04_ball_present(void)
{
    float distance_cm = hcsr04_get_distance_cm();
    if (distance_cm < 0.0f) {
        return false;
    }
    return distance_cm <= BALL_PRESENT_MAX_DISTANCE_CM;
}