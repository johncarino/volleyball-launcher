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
