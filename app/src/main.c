// Main program: DC motor speed control via PWM on ZS-X11H driver board.
// Runs on BeagleY-AI.
// Wiring: P (signal) and G (ground) connected to GPIO12 (pin 32, PWM0).

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>

#include "hal/pwm.h"

// -------------------------------------------------------
// Configuration — adjust to match your BeagleY-AI
// -------------------------------------------------------
// SSH into the board and run:  ls /sys/class/pwm/
// to find the correct pwmchip number.
// GPIO12 (pin 32) = PWM0 channel B
#define PWMCHIP     0
#define PWM_CHANNEL 1       // channel B of PWM0 (GPIO12)

static volatile bool s_running = true;

static void signal_handler(int sig)
{
    (void)sig;
    s_running = false;
}

int main(void)
{
    printf("=== DC Motor Speed Controller (ZS-X11H) ===\n");

    // Catch Ctrl-C for clean shutdown
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    // Initialize PWM on GPIO12 (pin 32)
    if (pwm_init(PWMCHIP, PWM_CHANNEL) != 0) {
        fprintf(stderr, "Failed to initialize PWM. Are you running as root?\n");
        return 1;
    }

    // Set PWM frequency to 1 kHz (board accepts 50 Hz – 20 kHz)
    pwm_set_frequency(1000);

    // Start with motor stopped
    pwm_set_duty_cycle(0);
    pwm_enable(true);

    // Demo: ramp up, hold, ramp down
    printf("\n--- Ramping UP (0%% -> 50%%) ---\n");
    for (int duty = 0; duty <= 50 && s_running; duty += 5) {
        pwm_set_duty_cycle(duty);
        sleep(1);
    }

    if (s_running) {
        printf("\n--- Holding at 50%% for 3 seconds ---\n");
        sleep(3);
    }

    printf("\n--- Ramping DOWN (50%% -> 0%%) ---\n");
    for (int duty = 50; duty >= 0 && s_running; duty -= 5) {
        pwm_set_duty_cycle(duty);
        sleep(1);
    }

    // Clean shutdown
    pwm_set_duty_cycle(0);
    pwm_enable(false);
    pwm_cleanup();

    printf("\n=== Motor stopped. Done. ===\n");
    return 0;
}