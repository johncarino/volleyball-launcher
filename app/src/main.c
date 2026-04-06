// Volleyball launcher main application.
// Runs on BeagleY-AI.

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>

#include "fsm.h"

int main(void) {
    printf("=== Volleyball Launcher Main Application ===\n");

    fsm_state_t main_state;

    fsm_init(&main_state);

    while (true) {
        if (fsm_update(&main_state) == 0) {
            break;
        }
    }

    printf("Volleyball Launcher shutting down.\n");
    return 0;
}