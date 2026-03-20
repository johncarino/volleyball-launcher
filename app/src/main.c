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
        fprintf(stderr, "Failed to initialize PWM. Are you running as root?\n");
        return 1;
    }

    forward_ms(50, 1000); // Forward at 50% for 1 second
    reverse_ms(50, 1000); // Reverse at 50% for 1 second

    pwm_cleanup();
    return 0;
}