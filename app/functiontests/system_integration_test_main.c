// Integrated function test for launcher subsystems:
// - MCP4725 (BLDC launcher speed via analog throttle)
// - BTS7960 (linear actuator angle adjustment)
// - TB6600  (launcher rotation)

#include <ctype.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "hal/bts7960.h"
#include "hal/mcp4725.h"
#include "hal/tb6600.h"

#define LAUNCHER_MIN_MV 1500
#define LAUNCHER_MAX_MV 3900
#define ACTUATOR_SPEED_PERCENT 70
#define DEFAULT_MS_PER_10MM 300
#define DEFAULT_ROT_STEPS 100
#define ROT_STEP_DELAY_US 500

static volatile sig_atomic_t s_running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    s_running = 0;
}

static bool is_quit_token(const char *s)
{
    if (s == NULL) {return false;}
    return (strcmp(s, "q") == 0) || (strcmp(s, "Q") == 0) || (strcmp(s, "exit") == 0) || (strcmp(s, "EXIT") == 0);
}

static void trim_newline(char *s)
{
    size_t n;

    if (s == NULL) {return;}

    n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

static int read_line(char *buf, size_t size)
{
    if (!s_running) {return -1;}
    if (fgets(buf, (int)size, stdin) == NULL) {return -1;}
    trim_newline(buf);
    return 0;
}

static int parse_int_strict(const char *s, int *out)
{
    long val;
    char *endptr = NULL;

    if (s == NULL || out == NULL || s[0] == '\0') {return -1;}

    val = strtol(s, &endptr, 10);
    if (*endptr != '\0') {return -1;}
    if (val < -2147483648L || val > 2147483647L) {return -1;}
    *out = (int)val;
    return 0;
}

static int parse_double_strict(const char *s, double *out)
{
    double val;
    char *endptr = NULL;

    if (s == NULL || out == NULL || s[0] == '\0') {return -1;}

    val = strtod(s, &endptr);
    if (*endptr != '\0') {return -1;}

    *out = val;
    return 0;
}

static void print_main_menu(void)
{
    printf("\n Volleyball Launcher Integration Test \n");
    printf("1) launcher  - MCP4725 BLDC speed control\n");
    printf("2) angle     - BTS7960 linear actuator control\n");
    printf("3) rotation  - TB6600 launcher rotation\n");
    printf("q) quit      - q / Q / exit\n");
    printf("Select option: ");
    fflush(stdout);
}

static int launcher_menu(void)
{
    char input[128];

    while (s_running) {
        double volts;
        int mv;

        printf("\n[launcher]\n");
        printf("Enter voltage %.1f - %.1f V (recommended ~2.8-3.0 V)\n",
               LAUNCHER_MIN_MV / 1000.0,
               LAUNCHER_MAX_MV / 1000.0);
        printf("Type 'b' to return to main menu, or q/Q/exit to quit: ");
        fflush(stdout);

        if (read_line(input, sizeof(input)) != 0) {
            return -1;
        }

        if (strcmp(input, "b") == 0 || strcmp(input, "B") == 0) {
            return 0;
        }
        if (is_quit_token(input)) {
            return -1;
        }

        if (parse_double_strict(input, &volts) != 0) {
            printf("Invalid input. Please enter a numeric voltage.\n");
            continue;
        }

        mv = (int)(volts * 1000.0 + 0.5);
        if (mv < LAUNCHER_MIN_MV || mv > LAUNCHER_MAX_MV) {
            printf("Out of range. Please enter %.1f - %.1f V.\n", LAUNCHER_MIN_MV / 1000.0, LAUNCHER_MAX_MV / 1000.0);
            continue;
        }

        if (mcp4725_set_mv((uint16_t)mv) != 0) {
            fprintf(stderr, "Failed to set launcher voltage to %.3f V\n", mv / 1000.0);
            continue;
        }

        printf("Launcher voltage set to %.3f V\n", mv / 1000.0);
    }
    return -1;
}

static int angle_menu(int ms_per_10mm)
{
    char input[128];

    while (s_running) {
        printf("\n[angle]\n");
        printf("Commands: e=extend +10mm, r=retract -10mm, b=back, q/Q/exit=quit\n");
        printf("Actuator speed fixed at %d%%, duration per 10mm: %d ms\n", ACTUATOR_SPEED_PERCENT, ms_per_10mm);
        printf("Enter command: ");
        fflush(stdout);

        if (read_line(input, sizeof(input)) != 0) {return -1;}
        if (strcmp(input, "b") == 0 || strcmp(input, "B") == 0) {return 0;}
        if (is_quit_token(input)) {return -1;}

        if (strcmp(input, "e") == 0 || strcmp(input, "E") == 0) {
            if (forward_ms(ACTUATOR_SPEED_PERCENT, ms_per_10mm) != 0) {
                fprintf(stderr, "Extend command failed\n");
            } else {
                printf("Extended by ~10 mm\n");
            }
            continue;
        }

        if (strcmp(input, "r") == 0 || strcmp(input, "R") == 0) {
            if (reverse_ms(ACTUATOR_SPEED_PERCENT, ms_per_10mm) != 0) {
                fprintf(stderr, "Retract command failed\n");
            } else {
                printf("Retracted by ~10 mm\n");
            }
            continue;
        }
        printf("Unknown command. Use e/r/b/q.\n");
    }
    return -1;
}

static int rotation_menu(tb6600_t *motor, int steps_per_increment)
{
    char input[128];

    while (s_running) {
        printf("\n[rotation]\n");
        printf("Commands: cw=clockwise, ccw=counter-clockwise, b=back, q/Q/exit=quit\n");
        printf("Increment size: %d steps\n", steps_per_increment);
        printf("Enter command: ");
        fflush(stdout);

        if (read_line(input, sizeof(input)) != 0) {return -1;}
        if (strcmp(input, "b") == 0 || strcmp(input, "B") == 0) {return 0;}
        if (is_quit_token(input)) {return -1;}

        if (strcmp(input, "cw") == 0 || strcmp(input, "CW") == 0) {
            tb6600_set_direction(motor, 1);
            tb6600_step(motor, steps_per_increment, ROT_STEP_DELAY_US);
            printf("Rotated clockwise by %d steps\n", steps_per_increment);
            continue;
        }

        if (strcmp(input, "ccw") == 0 || strcmp(input, "CCW") == 0) {
            tb6600_set_direction(motor, 0);
            tb6600_step(motor, steps_per_increment, ROT_STEP_DELAY_US);
            printf("Rotated counter-clockwise by %d steps\n", steps_per_increment);
            continue;
        }

        printf("Unknown command. Use cw/ccw/b/q.\n");
    }
    return -1;
}

int main(void)
{
    tb6600_t motor;
    char input[128];
    int ms_per_10mm = DEFAULT_MS_PER_10MM;
    int steps_per_increment = DEFAULT_ROT_STEPS;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("System Integration Test\n");

    if (bts_init() != 0) {
        fprintf(stderr, "Failed to initialize BTS7960\n");
        return 1;
    }

    if (mcp4725_init(MCP4725_I2C_BUS, MCP4725_I2C_ADDR) != 0) {
        fprintf(stderr, "Failed to initialize MCP4725\n");
        bts_cleanup();
        return 1;
    }

    if (tb6600_init(&motor, 1) != 0) {
        fprintf(stderr, "Failed to initialize TB6600\n");
        mcp4725_cleanup();
        bts_cleanup();
        return 1;
    }

    tb6600_enable(&motor, 1);

    printf("\nInitialization complete.\n");
    printf("Launcher: %.1f-%.1f V allowed (start spin ~1.5 V, cutoff >3.9 V).\n", LAUNCHER_MIN_MV / 1000.0, LAUNCHER_MAX_MV / 1000.0);
    printf("Angle: 10mm increments at %d%% duty.\n", ACTUATOR_SPEED_PERCENT);
    printf("Rotation: %d-step increments.\n", steps_per_increment);

    printf("\nOptional calibration: enter milliseconds for one 10mm actuator move\n");
    printf("(press Enter to keep default %d ms): ", ms_per_10mm);
    fflush(stdout);
    if (read_line(input, sizeof(input)) == 0 && input[0] != '\0') {
        int parsed = 0;
        if (parse_int_strict(input, &parsed) == 0 && parsed > 0) {
            ms_per_10mm = parsed;
            printf("Using %d ms per 10mm\n", ms_per_10mm);
        } else {
            printf("Invalid value, using default %d ms\n", ms_per_10mm);
        }
    }

    while (s_running) {
        print_main_menu();
        if (read_line(input, sizeof(input)) != 0) {break;}
        if (is_quit_token(input)) {break;}

        if (strcmp(input, "1") == 0 || strcmp(input, "launcher") == 0) {
            if (launcher_menu() != 0) {
                break;
            }
            continue;
        }

        if (strcmp(input, "2") == 0 || strcmp(input, "angle") == 0) {
            if (angle_menu(ms_per_10mm) != 0) {
                break;
            }
            continue;
        }

        if (strcmp(input, "3") == 0 || strcmp(input, "rotation") == 0) {
            if (rotation_menu(&motor, steps_per_increment) != 0) {
                break;
            }
            continue;
        }
        printf("Unknown selection. Use 1/2/3 or launcher/angle/rotation or q.\n");
    }

    printf("\nShutting down and freeing resources...\n");
    mcp4725_set_raw(0);
    mcp4725_cleanup();
    bts_cleanup();
    tb6600_enable(&motor, 0);
    tb6600_close(&motor);

    printf("Done.\n");
    return 0;
}
