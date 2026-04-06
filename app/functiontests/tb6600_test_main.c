// TB6600 stepper motor driver interactive test program

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hal/tb6600.h"

#include <gpiod.h>
#include <unistd.h> // usleep


int main(void)
{
    tb6600_t motor;
    char input[64];
    int steps, direction, start_delay, end_delay, accel_steps;

    printf("=== TB6600 Stepper Motor Interactive Test ===\n");

    if (tb6600_init(&motor, 1) < 0) {
        fprintf(stderr, "Failed to initialize TB6600\n");
        return -1;
    }

    tb6600_enable(&motor, 1);
    printf("Motor enabled.\n\n");

    while (1) {
        // --- Direction ---
        printf("Direction (0 = reverse, 1 = forward) [1]: ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) break;
        direction = (input[0] == '\n') ? 1 : atoi(input);
        if (direction != 0 && direction != 1) {
            printf("Invalid direction. Must be 0 or 1.\n");
            continue;
        }

        // --- Steps ---
        printf("Number of steps [700]: ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) break;
        steps = (input[0] == '\n') ? 700 : atoi(input);
        if (steps <= 0) {
            printf("Steps must be > 0.\n");
            continue;
        }

        // --- Start delay ---
        printf("Start delay in us (slow speed) [2000]: ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) break;
        start_delay = (input[0] == '\n') ? 2000 : atoi(input);

        // --- End delay ---
        printf("End delay in us (cruise speed) [500]: ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) break;
        end_delay = (input[0] == '\n') ? 500 : atoi(input);

        // --- Accel steps ---
        printf("Acceleration steps [100]: ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) break;
        accel_steps = (input[0] == '\n') ? 100 : atoi(input);

        // --- Execute move ---
        printf("\nMoving %s %d steps  (start %d us -> %d us, accel %d steps)\n",
               direction ? "FORWARD" : "REVERSE",
               steps, start_delay, end_delay, accel_steps);

        tb6600_set_direction(&motor, direction);
        tb6600_step_accel(&motor, steps, start_delay, end_delay, accel_steps);

        printf("Move complete. Holding position for 1 second...\n");
        usleep(1000000);

        // --- Continue? ---
        printf("\nRun another move? (y/n) [y]: ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) break;
        if (input[0] == 'n' || input[0] == 'N') break;

        printf("\n");
    }

    tb6600_enable(&motor, 0);
    tb6600_close(&motor);

    printf("\n=== TB6600 test completed ===\n");
    return 0;
}
