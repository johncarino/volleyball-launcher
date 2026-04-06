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
    // 400 steps = 180 degrees (half turn).
    // Ramp from 2000us (slow) down to 600us (cruise) over 100 accel steps.
    // Lazy susan bearing takes axial load off the motor shaft.
    printf("Forward 400 steps (180 deg) with acceleration ramp\n");
    tb6600_set_direction(&motor, 1);
    tb6600_step_accel(&motor, 400, 2000, 600, 100);

    // Let the load come to a complete stop before reversing.
    // Motor stays enabled so it actively holds position against inertia.
    printf("Holding position for 3 seconds...\n");
    usleep(3000000);

    // Small settling delay after direction change to avoid reverse-stall
    printf("Reverse 400 steps (180 deg) with acceleration ramp\n");
    tb6600_set_direction(&motor, 0);
    usleep(50000);  // 50ms settle after DIR change
    tb6600_step_accel(&motor, 400, 2000, 600, 100);

    // Hold position after the last move so the load doesn't freewheel.
    printf("Holding position for 3 seconds...\n");
    usleep(3000000);

    tb6600_enable(&motor, 0);
    tb6600_close(&motor);

    printf("=== TB6600 test completed ===\n");
    return 0;
}
