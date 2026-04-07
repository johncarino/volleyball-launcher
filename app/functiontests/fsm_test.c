// Volleyball launcher: two DC motors controlled via PWM on ZS-X11H boards.
// Runs on BeagleY-AI.

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>

#include "fsm.h"


int main(void) {
    printf("=== Volleyball Launcher Main Application ===\n");

    fsm_state_t main_state;

    //printf("init main\n");
    fsm_init(&main_state);

    int user_input = 0;

    while (true) {

        if (fsm_update(&main_state) == 0) {
            break;
        }

        scanf("%d", &user_input);

        fsm_handle_input(&main_state, user_input);

        sleep(1); // Sleep to prevent busy-waiting
    }

    printf("Goodbye! Shutting down the Volleyball Launcher.\n");

    return 0;
}