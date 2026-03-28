#include "fsm.h"

void fsm_init(fsm_state_t *state) {
    state->mode = MODE_CALIBRATION;

    arc_calc_params(STANDARD_NET_HEIGHT, STANDARD_COURT_WIDTH, STANDARD_COURT_LENGTH);
}

int fsm_update(fsm_state_t *state) {
    switch (state->mode) {
        case MODE_CALIBRATION:
            printf("Entered Calibration Mode\n");
            //logic
            char input;
            float value;
            while (true) {
            printf("Change net height (w), court width (e), or court length (r)? q to quit\n");
            scanf(" %c", &input);
            if (input == 'q') {
                break;
            }
            printf("Enter new value:\n");
            scanf("%f", &value);
            calibrate_user_input(input, value);
            }
            state->mode = MODE_SET;
            fsm_update(state);  // Invoke again after mode change
            break;
        case MODE_SET:
            printf("Entered Set Mode\n");
            //logic
            int machine_pos, target_loc, tempo, set_index;
            char cont;
            while (true) {
                printf("Choose machine position (0 for left, 1 for center, 2 for right):\n");
                scanf("%d", &machine_pos);
                set_machine_position(machine_pos);
                common_sets();
                printf("start operation with predefined sets? (y/n)\n");
                scanf(" %c", &cont);
                if (cont == 'y') {
                    state->mode = MODE_OPERATION;
                    return fsm_update(state);
                }
                printf("Choose target location (0-4):\n");
                scanf("%d", &target_loc);
                choose_target_location(target_loc);
                printf("Choose tempo (0-3):\n");
                scanf("%d", &tempo);
                choose_tempo(tempo);
                printf("Save set to slot 0~3?\n");
                scanf("%d", &set_index);
                save_set(set_index);
                printf("Set saved. Do you want to save another set? (y/n)\n");
                scanf(" %c", &cont);
                if (cont == 'n') {
                    printf("Choose next mode: operation(o), calibration(c), quit(q)\n");
                    scanf(" %c", &cont);
                    switch(cont) {
                        case 'o':
                            state->mode = MODE_OPERATION;
                            return fsm_update(state);
                        case 'c':
                            state->mode = MODE_CALIBRATION;
                            return fsm_update(state);
                        case 'q':
                            return 0;
                        default:
                            printf("Invalid input. Please try again.\n");
                    }
                }
            }
            break;
        case MODE_ADVANCED:
            printf("Entered Advanced Mode\n");
            //logic
            break;
        case MODE_OPERATION:
            printf("Entered Operation Mode\n");
            operation_init();
            set_machine(0);

            char set_n;

            while (true) {
                printf("Enter set 0 to 3, or q: ");
                scanf(" %c", &set_n);

                if (set_n == 'q') {
                    break;
                }

                int n = set_n - '0';

                if (n >= 0 && n <= 3) {
                    set_machine(n);
                } else {
                    printf("Invalid input\n");
                }
            }
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