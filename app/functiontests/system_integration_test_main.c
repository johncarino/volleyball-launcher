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
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

#include "hal/bts7960.h"
#include "hal/mcp4725.h"
#include "hal/tb6600.h"

#define LAUNCHER_MIN_MV 1500
#define LAUNCHER_MAX_MV 3900
#define ACTUATOR_DUTY 100
#define DEFAULT_ROT_STEPS 100
#define ROT_STEP_DELAY_US 500

static volatile sig_atomic_t s_running = 1;
static mcp4725_t dac1 = MCP4725_INIT_ZERO;

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

        if (mcp4725_set_mv(&dac1, (uint16_t)mv) != 0) {
            fprintf(stderr, "Failed to set launcher voltage to %.3f V\n", mv / 1000.0);
            continue;
        }

        printf("Launcher voltage set to %.3f V\n", mv / 1000.0);
    }
    return -1;
}

static int angle_menu(void)
{
    struct termios old_tio, new_tio;
    int moving = 0; // 0=stopped, 1=forward, -1=reverse

    printf("\n[angle] Hold UP arrow to extend, DOWN arrow to retract.\n");
    printf("Press 'b' to return to main menu, 'q' to quit.\n");
    printf("Actuator runs at %d%% duty while key is held.\n", ACTUATOR_DUTY);
    fflush(stdout);

    // Switch terminal to raw mode with timeout
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ICANON | ECHO);
    new_tio.c_cc[VMIN] = 0;
    new_tio.c_cc[VTIME] = 1; // 100ms timeout
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

    while (s_running) {
        unsigned char buf[8];
        int n = read(STDIN_FILENO, buf, sizeof(buf));

        if (n > 0) {
            // Check for 'b' or 'q' single-char commands
            if (n == 1 && (buf[0] == 'b' || buf[0] == 'B')) {
                bts_stop();
                tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
                printf("\n");
                return 0;
            }
            if (n == 1 && (buf[0] == 'q' || buf[0] == 'Q')) {
                bts_stop();
                tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
                printf("\n");
                return -1;
            }

            // Arrow keys: ESC [ A (up) / ESC [ B (down)
            if (n >= 3 && buf[0] == 0x1b && buf[1] == '[') {
                if (buf[2] == 'A') { // UP arrow = extend/forward
                    if (moving != 1) {
                        bts_forward_start(ACTUATOR_DUTY);
                        moving = 1;
                    }
                    continue;
                }
                if (buf[2] == 'B') { // DOWN arrow = retract/reverse
                    if (moving != -1) {
                        bts_reverse_start(ACTUATOR_DUTY);
                        moving = -1;
                    }
                    continue;
                }
            }
        } else {
            // Timeout — no key held, stop motor
            if (moving != 0) {
                bts_stop();
                moving = 0;
            }
        }
    }

    bts_stop();
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
    printf("\n");
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
    int steps_per_increment = DEFAULT_ROT_STEPS;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("System Integration Test\n");

    if (bts_init() != 0) {
        fprintf(stderr, "Failed to initialize BTS7960\n");
        return 1;
    }

    if (mcp4725_init(&dac1, MCP4725_I2C_BUS1, MCP4725_I2C_ADDR) != 0) {
        fprintf(stderr, "Failed to initialize MCP4725\n");
        bts_cleanup();
        return 1;
    }

    if (tb6600_init(&motor, 1) != 0) {
        fprintf(stderr, "Failed to initialize TB6600\n");
        mcp4725_cleanup(&dac1);
        bts_cleanup();
        return 1;
    }

    tb6600_enable(&motor, 1);

    printf("\nInitialization complete.\n");
    printf("Launcher: %.1f-%.1f V allowed (start spin ~1.5 V, cutoff >3.9 V).\n", LAUNCHER_MIN_MV / 1000.0, LAUNCHER_MAX_MV / 1000.0);
    printf("Angle: hold UP/DOWN arrow keys at %d%% duty.\n", ACTUATOR_DUTY);
    printf("Rotation: %d-step increments.\n", steps_per_increment);

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
            if (angle_menu() != 0) {
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
    mcp4725_set_raw(&dac1, 0);
    mcp4725_cleanup(&dac1);
    bts_cleanup();
    tb6600_enable(&motor, 0);
    tb6600_close(&motor);

    printf("Done.\n");
    return 0;
}
