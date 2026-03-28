#ifndef FSM_H
#define FSM_H

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "calibration.h"
#include "set.h"
#include "operation.h"

/*
* Finite State Machine Header
*
* Modes of operation:
* 1. Calibration Mode: Define Parameters such as net height and court dimensions.
* 2. Set Mode: Define machine location, set target location, and set tempo.
* 3. Advanced Mode: Define specific launch angles and speeds.
* 4. Operation Mode: Execute launches based on the defined parameters.
*/

typedef enum {
    MODE_CALIBRATION,
    MODE_SET,
    MODE_ADVANCED,
    MODE_OPERATION
} fsm_mode_t;

typedef struct {
    fsm_mode_t mode;
    // Additional parameters?
    // previous mode
} fsm_state_t;

void fsm_init(fsm_state_t *state);

int fsm_update(fsm_state_t *state);

void fsm_handle_input(fsm_state_t *state, int input);

fsm_state_t fsm_get_state();

#endif // FSM_H