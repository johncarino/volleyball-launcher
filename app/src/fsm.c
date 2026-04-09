#include "fsm.h"

static bool read_token(char *buf) {
    return scanf(" %31s", buf) == 1;
}

static bool is_quit_token(const char *token) {
    return (token[0] == 'q' || token[0] == 'Q') && token[1] == '\0';
}

static bool parse_int_token(const char *token, int *out) {
    char *endptr = NULL;
    long value = strtol(token, &endptr, 10);

    if (endptr == token || *endptr != '\0') {
        return false;
    }

    *out = (int)value;
    return true;
}

static bool parse_float_token(const char *token, float *out) {
    char *endptr = NULL;
    float value = strtof(token, &endptr);

    if (endptr == token || *endptr != '\0') {
        return false;
    }

    *out = value;
    return true;
}

void fsm_init(fsm_state_t *state) {
    
    state->mode = MODE_CALIBRATION;

    arc_calc_params(STANDARD_NET_HEIGHT, STANDARD_COURT_WIDTH, STANDARD_COURT_LENGTH);

    printf("============= DIME TIME =============\n");
    printf("Welcome to the Volleyball Launcher!\n");
    printf("Please follow the prompts to calibrate the machine and set up your desired launch parameters.\n");
    printf("You can quit at any time by entering 'q'.\n");
    printf("=====================================\n\n");

    sleep(2); // Pause for 2 seconds before starting the FSM loop
}

int fsm_update(fsm_state_t *state) {
    char token[32];

    switch (state->mode) {
        case MODE_CALIBRATION: {
            char input = '\0';
            float value;
            printf("Entering Calibration Mode. You can adjust the following parameters:\n");
            while (true) {
            printf("Change net height (w), court width (e), or court length (r)\n");
            printf("Net height: %.2f m\n", get_net_height());
            printf("Court width: %.2f m\n", get_court_width());
            printf("Court length: %.2f m\n", get_court_length());
            printf("Enter 't' to continue with the current parameters.\n");

            if (!read_token(token)) {
                return 0;
            }
            if (is_quit_token(token)) {
                return 0;
            }
            if (token[1] == '\0' && (token[0] == 't' || token[0] == 'T')) {
                state->mode = MODE_SET;
                return fsm_update(state);
            }
            if (token[1] != '\0' || (token[0] != 'w' && token[0] != 'e' && token[0] != 'r')) {
                printf("Invalid input. Enter w, e, r, t, or q.\n");
                continue;
            }

            input = token[0];
            printf("Enter new value:\n");
            if (!read_token(token)) {
                return 0;
            }
            if (is_quit_token(token)) {
                return 0;
            }
            if (!parse_float_token(token, &value)) {
                printf("Invalid value. Please enter a number.\n");
                continue;
            }
            calibrate_user_input(input, value);
            }
            state->mode = MODE_SET;
            fsm_update(state);  // Invoke again after mode change
            break;
        }
        case MODE_SET:
            printf("Entering Set Mode.\n");
            printf("In Set Mode, you can define machine position, target location, and tempo for your sets.\n");

            //logic
            int machine_pos, target_loc, tempo, set_index;
            char cont;

            while (true) {
                printf("Choose machine position (0-2):\n");
                if (!read_token(token)) {
                    return 0;
                }
                if (is_quit_token(token)) {
                    return 0;
                }
                if (!parse_int_token(token, &machine_pos)) {
                    printf("Invalid input. Please enter 0, 1, 2, or q.\n");
                    continue;
                }
                set_machine_position(machine_pos);
                common_sets();
                break;
            }

            while (true) {
                printf("The current sets are:\n");
                print_sets();
                printf("\nTo begin operation with the current sets, enter 'y'. To customize sets, enter 'n'.\n");
                printf("To return to calibration mode, enter 'c'.\n");
                if (!read_token(token)) {
                    return 0;
                }
                if (is_quit_token(token)) {
                    return 0;
                }
                if (token[1] != '\0') {
                    printf("Invalid input. Please enter y, n, or q.\n");
                    continue;
                }
                cont = token[0];
                if (cont == 'y') {
                    state->mode = MODE_OPERATION;
                    return fsm_update(state);
                }
                if (cont == 'c') {
                    state->mode = MODE_CALIBRATION;
                    return fsm_update(state);
                }
                printf("Target Location:     Tempo:\n");
                printf("Choose target location (0-4)\n");
                if (!read_token(token)) {
                    return 0;
                }
                if (is_quit_token(token)) {
                    return 0;
                }
                if (!parse_int_token(token, &target_loc)) {
                    printf("Invalid input. Please enter 0-4.\n");
                    continue;
                }
                if (target_loc < 0 || target_loc > 4) {
                    printf("Invalid input. Please enter 0-4.\n");
                    continue;
                }
                choose_target_location(target_loc);
                printf("Target Location: %d   Tempo:\n", target_loc);
                printf("Choose tempo (1-4).\n");
                if (!read_token(token)) {
                    return 0;
                }
                if (is_quit_token(token)) {
                    return 0;
                }
                if (!parse_int_token(token, &tempo)) {
                    printf("Invalid input. Please enter 1-4.\n");
                    continue;
                }
                if (tempo < 1 || tempo > 4) {
                    printf("Invalid input. Please enter 1-4.\n");
                    continue;
                }
                choose_tempo(tempo);
                printf("Target Location: %d   Tempo: %d\n", target_loc, tempo);
                printf("Save set to slot 0~3:\n");
                if (!read_token(token)) {
                    return 0;
                }
                if (is_quit_token(token)) {
                    return 0;
                }
                if (!parse_int_token(token, &set_index)) {
                    printf("Invalid input. Please enter 0-3.\n");
                    continue;
                }
                if (set_index < 0 || set_index > 3) {
                    printf("Invalid input. Please enter 0-3.\n");
                    continue;
                }
                if (!save_set(set_index, 1)) {
                    printf("Failed to save set to slot %d.\n", set_index);
                } else {
                    printf("Set saved to slot %d.\n", set_index);
                }
                sleep(2); // Pause for 2 seconds before showing the menu again
            }
            break;
        case MODE_ADVANCED:
            printf("Entered Advanced Mode\n");
            //logic
            break;
        case MODE_OPERATION:
            printf("Entering Operation Mode\n");
            printf("In Operation Mode, the machine will execute launches based on the defined parameters in Set Mode.\n");
            printf("You can return to Set Mode at any time by entering 's'.\n");
            printf("As always, you can quit at any time by entering 'q'.\n");

            operation_init();

            char set_n;

            while (true) {
                printf("Enter set 0 to 3: \n");
                printf("You can return to Set Mode at any time by entering 's'.\n");
                printf("you can quit at any time by entering 'q'.\n");
                
                if (!read_token(token)) {
                    operation_cleanup();
                    return 0;
                }
                if (is_quit_token(token)) {
                    operation_cleanup();
                    return 0;
                }
                if (strcmp(token, "dev") == 0) {
                    printf("Entering Developer Mode...\n");
                    while (true) {
                        float tilt, yaw, speed;
                        printf("Set tilt, yaw, or rpm? (t/y/s) Enter 'b' to go back.\n");
                        if (!read_token(token)) {
                            continue;
                        }
                        if (is_quit_token(token)) {
                            continue;
                        }
                        if (token[1] != '\0') {
                            printf("Invalid input. Please enter t, y, s, b, or q.\n");
                            continue;
                        }
                        char param = token[0];
                        if (param == 'b' || param == 'B') {
                            break;
                        }
                        if (param == 't' || param == 'T') {
                            printf("Enter tilt angle in degrees (5 - 85):\n");
                            if (!read_token(token)) {
                                continue;
                            }
                            if (is_quit_token(token)) {
                                continue;
                            }
                            if (!parse_float_token(token, &tilt)) {
                                printf("Invalid input. Please enter a number.\n");
                                continue;
                            }
                            tilt_signal(tilt);
                            continue;
                        }
                        if (param == 'y' || param == 'Y') {
                            printf("Enter yaw angle in degrees (-90 to 90):\n");
                            if (!read_token(token)) {
                                continue;
                            }
                            if (is_quit_token(token)) {
                                continue;
                            }
                            if (!parse_float_token(token, &yaw)) {
                                printf("Invalid input. Please enter a number.\n");
                                continue;
                            }
                            yaw_signal(yaw);
                            continue;
                        }
                        if (param == 's' || param == 'S') {
                            printf("Enter speed in RPM (0 - 1130):\n");
                            if (!read_token(token)) {
                                continue;
                            }
                            if (is_quit_token(token)) {
                                continue;
                            }
                            if (!parse_float_token(token, &speed)) {
                                printf("Invalid input. Please enter a number.\n");
                                continue;
                            }
                            speed_signal(speed);
                            continue;
                        }
                    }
                    continue;
                }
                if (token[1] != '\0') {
                    printf("Invalid input\n");
                    continue;
                }

                set_n = token[0];

                if (set_n == 's' || set_n == 'S') {
                    break;
                }

                int n = set_n - '0';

                if (n >= 0 && n <= 3) {
                    printf("Running set %d\n", n);
                    set_machine(n);
                } else {
                    printf("Invalid input\n");
                }
            }
            operation_cleanup();

            state->mode = MODE_SET;
            fsm_update(state);  // Invoke again after mode change
            break;
    }
    return 0;
}

void fsm_handle_input(fsm_state_t *state, int input) {
    switch (input) {
        case 1:
            state->mode = MODE_CALIBRATION;
            break;
        case 2:
            state->mode = MODE_SET;
            break;
        case 3:
            printf("In production...come back later\n");
            //state->mode = MODE_ADVANCED;
            break;
        case 4:
            state->mode = MODE_OPERATION;
            break;
        default:
            printf("Invalid input. Please try again.\n");
    }
}