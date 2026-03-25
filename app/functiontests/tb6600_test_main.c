// TB6600 stepper motor driver test program

#include <stdio.h>
#include "hal/tb6600.h"

#include <gpiod.h>
#include <unistd.h> // usleep


int main(void)
{
    tb6600_t motor;

    printf("=== TB6600 Stepper Motor Test ===\n");

    if (tb6600_init(&motor, 1) < 0) {
        fprintf(stderr, "Failed to initialize TB6600\n");
        return -1;
    }

    tb6600_enable(&motor, 1);

    usleep(1000000); // 1 second delay before starting

    // 800 pulses/rev at 1/4 microstep.
    // Ramp from 2000us (slow) down to 500us (cruise) over 100 accel steps.
    printf("Forward 700 steps with acceleration ramp\n");
    tb6600_set_direction(&motor, 1);
    tb6600_step_accel(&motor, 700, 2000, 500, 100);

    // Let the load come to a complete stop before reversing.
    // Motor stays enabled so it actively holds position against inertia.
    printf("Holding position for 2 seconds...\n");
    usleep(2000000);

    printf("Reverse 700 steps with acceleration ramp\n");
    tb6600_set_direction(&motor, 0);
    tb6600_step_accel(&motor, 700, 2000, 500, 100);

    // Hold position after the last move so the load doesn't freewheel.
    printf("Holding position for 2 seconds...\n");
    usleep(2000000);

    tb6600_enable(&motor, 0);
    tb6600_close(&motor);

    printf("=== TB6600 test completed ===\n");
    return 0;
}


/*
int main(void)
{
    const char *chipname = "/dev/gpiochip0";  // GPIO23 is on gpiochip0
    unsigned int offset = 7;                   // GPIO23 / physical pin 16
    struct gpiod_chip *chip = NULL;
    struct gpiod_line_config *line_config = NULL;
    struct gpiod_request_config *request_config = NULL;
    struct gpiod_line_request *request = NULL;
    struct gpiod_line_settings *settings = NULL;

    printf("Opening chip: %s\n", chipname);
    chip = gpiod_chip_open(chipname);
    if (!chip) {
        perror("Open chip failed");
        return -1;
    }
    printf("  chip opened OK\n");

    printf("Allocating line settings and configs...\n");
    settings = gpiod_line_settings_new();
    line_config = gpiod_line_config_new();
    request_config = gpiod_request_config_new();
    if (!settings || !line_config || !request_config) {
        fprintf(stderr, "Failed to create configs\n");
        goto fail;
    }
    printf("  configs allocated OK\n");

    printf("Configuring offset %u as output (initial: LOW)...\n", offset);
    if (gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT) < 0 ||
        gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE) < 0) {
        perror("Failed to set line settings");
        goto fail;
    }
    printf("  direction=OUTPUT, initial value=INACTIVE (0V)\n");

    printf("Adding line offset %u to line config...\n", offset);
    if (gpiod_line_config_add_line_settings(line_config, &offset, 1, settings) < 0) {
        perror("Failed to add line settings");
        goto fail;
    }
    printf("  line config OK\n");

    printf("Requesting line from kernel (consumer=\"tb6600_test\")...\n");
    gpiod_request_config_set_consumer(request_config, "tb6600_test");
    request = gpiod_chip_request_lines(chip, request_config, line_config);
    if (!request) {
        perror("Request lines failed");
        goto fail;
    }
    printf("  lines requested OK — GPIO offset %u is now owned by this process\n", offset);

    // Blink: 1 second high, 1 second low, 10 cycles
    for (int i = 0; i < 10; i++) {
        printf("HIGH (3.3V)\n");
        if (gpiod_line_request_set_value(request, offset, GPIOD_LINE_VALUE_ACTIVE) < 0) {
            perror("Failed to set value");
            goto fail;
        }
        sleep(2);

        printf("LOW  (0V)\n");
        if (gpiod_line_request_set_value(request, offset, GPIOD_LINE_VALUE_INACTIVE) < 0) {
            perror("Failed to set value");
            goto fail;
        }
        sleep(1);
    }

    // Clean up
    gpiod_line_request_release(request);
    gpiod_line_config_free(line_config);
    gpiod_request_config_free(request_config);
    gpiod_line_settings_free(settings);
    gpiod_chip_close(chip);
    return 0;

fail:
    if (request) gpiod_line_request_release(request);
    if (line_config) gpiod_line_config_free(line_config);
    if (request_config) gpiod_request_config_free(request_config);
    if (settings) gpiod_line_settings_free(settings);
    if (chip) gpiod_chip_close(chip);
    return -1;
}

*/