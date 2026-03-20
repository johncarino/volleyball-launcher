// TB6600 stepper motor driver test program

#include <stdio.h>
#include "hal/tb6600.h"

int main(void)
{
    tb6600_t motor;

    printf("=== TB6600 Stepper Motor Test ===\n");

    if (tb6600_init(&motor, "gpiochip0", 1) < 0) {
        fprintf(stderr, "Failed to initialize TB6600\n");
        return -1;
    }

    tb6600_enable(&motor, 1);

    printf("Forward...\n");
    tb6600_set_direction(&motor, 1);
    tb6600_step(&motor, 200, 500); // 200 steps with 500us delay

    printf("Reverse...\n");
    tb6600_set_direction(&motor, 0);
    tb6600_step(&motor, 200, 500); // 200 steps with 500us delay

    tb6600_enable(&motor, 0);
    tb6600_close(&motor);

    printf("=== TB6600 test completed ===\n");
    return 0;
}
