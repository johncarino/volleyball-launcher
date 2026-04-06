//Testbench for bts7960 linear actuator driver

// Volleyball launcher: two DC motors controlled via PWM on ZS-X11H boards.
// Runs on BeagleY-AI.

//ASSUMING LINEAR
//171.111111... MS PER DEGREE FORWARD
//

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

    /* --- Interactive test --- */
    printf("\n--- Interactive mode (enter commands manually) ---\n");

    while (s_running) {
        char dir;
        int percent;
        long duration_ms;

        printf("\nEnter direction (f=forward, r=reverse, q=quit): ");
        if (scanf(" %c", &dir) != 1) break;
        if (dir == 'q' || dir == 'Q') break;
        if (dir != 'f' && dir != 'F' && dir != 'r' && dir != 'R') {
            printf("Invalid direction. Use 'f', 'r', or 'q'.\n");
            continue;
        }

        printf("Enter percent (0-100): ");
        if (scanf("%d", &percent) != 1) break;
        if (percent < 0 || percent > 100) {
            printf("Percent must be between 0 and 100.\n");
            continue;
        }

        printf("Enter duration (ms): ");
        if (scanf("%ld", &duration_ms) != 1) break;
        if (duration_ms <= 0) {
            printf("Duration must be positive.\n");
            continue;
        }

        int rc;
        if (dir == 'f' || dir == 'F') {
            printf("\nForward %d%% for %ld ms...\n", percent, duration_ms);
            rc = forward_ms(percent, duration_ms);
        } else {
            printf("\nReverse %d%% for %ld ms...\n", percent, duration_ms);
            rc = reverse_ms(percent, duration_ms);
        }

        if (rc != 0) {
            fprintf(stderr, "Command failed\n");
            bts_cleanup();
            return 1;
        }
    }

    bts_cleanup();

    printf("\n=== Actuator test done. ===\n");
    return 0;
}
