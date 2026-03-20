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
    printf("=== Volleyball Launcher (2x ZS-X11H) ===\n");

    // Catch Ctrl-C for clean shutdown
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    if (pwm_init() != 0) {
        fprintf(stderr, "Failed to initialize BTS7960 HAL\n");
        return -1;
    }

    printf("Testing forward at 5%% for 2 seconds...\n");
    if (forward_ms(5, 2000) != 0) {
        fprintf(stderr, "Failed to run forward test\n");
        pwm_cleanup();
        return -1;
    }

    printf("Testing reverse at 10%% for 1 seconds...\n");
    if (reverse_ms(10, 1000) != 0) {
        fprintf(stderr, "Failed to run reverse test\n");
        pwm_cleanup();
        return -1;
    }

    // This also resets other PWM channels
    pwm_cleanup();
    printf("BTS7960 HAL test completed successfully\n");
    
    return 0;
}