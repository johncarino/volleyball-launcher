//Testbench for bts7960 linear actuator driver

#include <stdio.h>
#include "hal/bts7960.h"

int actuator_test() {
    if (bts_init() != 0) {
        fprintf(stderr, "Failed to initialize BTS7960 HAL\n");
        return -1;
    }

    printf("Testing forward at 5%% for 2 seconds...\n");
    if (forward_ms(5, 2000) != 0) {
        fprintf(stderr, "Failed to run forward test\n");
        bts_cleanup();
        return -1;
    }

    printf("Testing reverse at 10%% for 1 seconds...\n");
    if (reverse_ms(10, 1000) != 0) {
        fprintf(stderr, "Failed to run reverse test\n");
        bts_cleanup();
        return -1;
    }

    bts_cleanup();
    printf("BTS7960 HAL test completed successfully\n");
    return 0;
}

int main(){
    printf("=== BTS7960 Linear Actuator Test ===\n");
    return actuator_test();
}