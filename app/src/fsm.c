#include "fsm.h"

void fsm_init(fsm_state_t *state) {
    state->mode = MODE_CALIBRATION;
}

void fsm_update(fsm_state_t *state) {
    switch (state->mode) {
        case MODE_CALIBRATION:
            printf("Entered Calibration Mode\n");
            //logic
            break;
        case MODE_SET:
            printf("Entered Set Mode\n");
            //logic
            break;
        case MODE_ADVANCED:
            printf("Entered Advanced Mode\n");
            //logic
            break;
        case MODE_OPERATION:
            printf("Entered Operation Mode\n");
            //logic
            break;
    }
}

void fsm_handle_input(fsm_state_t *state, int input) {
    switch (input) {
        case 0:
            state->mode = MODE_CALIBRATION;
            break;
        case 1:
            state->mode = MODE_SET;
            break;
        case 2:
            state->mode = MODE_ADVANCED;
            break;
        case 3:
            state->mode = MODE_OPERATION;
            break;
    }
}