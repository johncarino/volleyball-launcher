//Testbench for bts7960 linear actuator driver

// Volleyball launcher: two DC motors controlled via PWM on ZS-X11H boards.
// Runs on BeagleY-AI.

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>

#include "hal/bts7960.h"

static volatile bool s_running = true;
static void signal_handler(int sig)
{
    (void)sig;
    s_running = false;
}

int main(void)
{
    printf("=== BTN/BTS7960 Linear Actuator Test ===\n");

    // Catch Ctrl-C for clean shutdown
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    if (bts_init() != 0) {
        fprintf(stderr, "Failed to initialize BTS/BTN7960 HAL. Are you running as root?\n");
        return 1;
    }

    printf("\nForward 100%% for 4s...\n");
    if (s_running && forward_ms(100, 4000) != 0) {
        fprintf(stderr, "Forward command failed\n");
        bts_cleanup();
        return 1;
    }

    if (s_running) {
        printf("Pause 2s...\n");
        sleep(2);
    }

    printf("Reverse 100%% for 4s...\n");
    if (s_running && reverse_ms(100, 4000) != 0) {
        fprintf(stderr, "Reverse command failed\n");
        bts_cleanup();
        return 1;
    }

    if (s_running) {
        printf("Pause 2s...\n");
        sleep(2);
    }

    bts_cleanup();

    printf("\n=== Actuator test done. ===\n");
    return 0;
}
