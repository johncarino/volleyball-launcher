//Testbench for bts7960 linear actuator driver

// Volleyball launcher: two DC motors controlled via PWM on ZS-X11H boards.
// Runs on BeagleY-AI.

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>

#include "hal/pwm.h"
//#include "hal/bts7960.h"

/*
int actuator_test() {
    if (bts_init() != 0) {
        fprintf(stderr, "Failed to initialize BTS7960 HAL\n");
        return -1;
    }

    printf("Testing forward at 50%% for 5 seconds...\n");
    if (forward_ms(50, 1000) != 0) {
        fprintf(stderr, "Failed to run forward test\n");
        bts_cleanup();
        return -1;
    }

    printf("Testing reverse at 20%% for 3 seconds...\n");
    if (reverse_ms(10, 30000) != 0) {
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
*/

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

    // Initialize both PWM channels
    if (pwm_init() != 0) {
        fprintf(stderr, "Failed to initialize PWM. Are you running as root?\n");
        return 1;
    }

    // Set PWM frequency to 1 kHz (shared by both motors)
    pwm_set_frequency(1000);

    // Start with both motors stopped
    pwm_set_duty_cycle(PWM_MOTOR_1, 0);
    pwm_set_duty_cycle(PWM_MOTOR_2, 0);
    pwm_enable(PWM_MOTOR_1, true);
    pwm_enable(PWM_MOTOR_2, true);

    // Demo: ramp both motors up together
    printf("\n--- Ramping BOTH motors UP (0%% -> 50%%) ---\n");
    for (int duty = 0; duty <= 50 && s_running; duty += 5) {
        pwm_set_duty_cycle(PWM_MOTOR_1, duty);
        pwm_set_duty_cycle(PWM_MOTOR_2, duty);
        sleep(1);
    }

    if (s_running) {
        printf("\n--- Holding at 50%% for 3 seconds ---\n");
        sleep(3);
    }

    // Ramp both motors down
    printf("\n--- Ramping BOTH motors DOWN (50%% -> 0%%) ---\n");
    for (int duty = 50; duty >= 0 && s_running; duty -= 5) {
        pwm_set_duty_cycle(PWM_MOTOR_1, duty);
        pwm_set_duty_cycle(PWM_MOTOR_2, duty);
        sleep(1);
    }

    // Clean shutdown
    pwm_set_duty_cycle(PWM_MOTOR_1, 0);
    pwm_set_duty_cycle(PWM_MOTOR_2, 0);
    pwm_enable(PWM_MOTOR_1, false);
    pwm_enable(PWM_MOTOR_2, false);
    pwm_cleanup();

    printf("\n=== Motors stopped. Done. ===\n");
    return 0;
}
