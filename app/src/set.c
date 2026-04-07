#include "set.h"

set_specs_t set_seq[NUM_SETS];

static int curr_machine_position = 0;
static int curr_target_location = 0;
static int curr_tempo = 0;

int set_machine_position(int position) {
    if (position < 0 || position >= NUM_MACHINE_POSITIONS) {
        fprintf(stderr, "Invalid machine position. Must be between 0 and %d.\n", NUM_MACHINE_POSITIONS - 1);
        return 0;
    }

    curr_machine_position = position;

    return position;

    //test
    //printf("Machine position set to %d.\n", machine_position);
}

int choose_target_location(int target) {
    if (target < 0 || target >= NUM_TARGETS) {
        fprintf(stderr, "Invalid target location. Must be between 0 and %d.\n", NUM_TARGETS - 1);
        return 0;
    }

    curr_target_location = target;

    return target;

    //test
    //printf("Target location set to %d.\n", target_position);
}

int choose_tempo(int tempo) {
    if (tempo < 0 || tempo >= NUM_TEMPOS) {
        fprintf(stderr, "Invalid tempo. Must be between 0 and %d.\n", NUM_TEMPOS - 1);
        return 0;
    }

    curr_tempo = tempo;
    
    return tempo;

    //test
    //printf("Tempo set to %d.\n", tempo);
}

void advanced_save_set(int set_index, float launch_speed, float tilt_angle, float yaw_angle, float rpm_output) {
    if (set_index < 0 || set_index >= NUM_SETS) {
        fprintf(stderr, "Invalid set index. Must be between 0 and %d.\n", NUM_SETS - 1);
        return;
    }
    set_seq[set_index].launch_speed = launch_speed;
    set_seq[set_index].tilt_angle = tilt_angle;
    set_seq[set_index].yaw_angle = yaw_angle;
    if (rpm_output == -1) {
        rpm_output = (launch_speed / (2*M_PI*WHEEL_R)) * 60 / EFF_K; //replace with a function in arc_calc
    }
    set_seq[set_index].rpm_output = rpm_output;

    set_seq[set_index].target_location = -1; //indicate custom set
    set_seq[set_index].tempo = -1; //indicate custom set

    //test
    printf("Set %d saved: Launch Speed = %.2f m/s, Tilt Angle = %.2f degrees, RPM Output = %.2f\n",
           set_index, launch_speed, tilt_angle * 180 / M_PI, rpm_output);
}

void save_set(int set_index) {
    set_seq[set_index].launch_speed = launch_speed[curr_machine_position][curr_target_location][curr_tempo];
    set_seq[set_index].tilt_angle = tilt_angle[curr_machine_position][curr_target_location][curr_tempo];
    set_seq[set_index].yaw_angle = yaw_angle[curr_machine_position][curr_target_location][curr_tempo];
    set_seq[set_index].rpm_output = rpm_output[curr_machine_position][curr_target_location][curr_tempo];
    set_seq[set_index].target_location = curr_target_location;
    set_seq[set_index].tempo = curr_tempo;
}

void common_sets() {
    switch(curr_machine_position) {
        case 0:
            curr_target_location = 1;
            curr_tempo = 0;
            save_set(0);
            curr_target_location = 1;
            curr_tempo = 3;
            save_set(1);
            curr_target_location = 4;
            save_set(2);
            curr_target_location = 2;
            curr_tempo = 2;
            save_set(3);
            break;
        case 2:
            curr_target_location = 0;
            curr_tempo = 1;
            save_set(0);
            curr_target_location = 0;
            curr_tempo = 3;
            save_set(1);
            curr_target_location = 3;
            save_set(2);
            curr_target_location = 2;
            curr_tempo = 2;
            save_set(3);
            break;
        default:
            curr_target_location = 0;
            curr_tempo = 1;
            save_set(0);
            curr_tempo = 2;
            save_set(1);
            curr_tempo = 3;
            save_set(2);
            curr_target_location = 2;
            curr_tempo = 2;
            save_set(3);
            break;
    }
}

void print_sets() {
    for (int i = 0; i < NUM_SETS; i++) {
        printf("Set %d: Target Location = %d, Tempo = %d\n", i, set_seq[i].target_location, set_seq[i].tempo);
    }
}