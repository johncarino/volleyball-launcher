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

    printf("Forward 20 steps with 500us delay\n");
    tb6600_set_direction(&motor, 1);
    tb6600_step(&motor, 1000, 500); // 20 steps with 500us delay

    printf("Reverse 20 steps with 500us delay\n");
    tb6600_set_direction(&motor, 0);
    tb6600_step(&motor, 1000, 500); // 20 steps with 500us delay

    tb6600_enable(&motor, 0);
    tb6600_close(&motor);

    printf("=== TB6600 test completed ===\n");
    return 0;
}
