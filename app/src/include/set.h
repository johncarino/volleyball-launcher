#ifndef SET_H
#define SET_H

#include <stdint.h>
#include "arc_calc.h"

/*
* Set Mode Header
*
* Operations within Set Mode:
* 1. Define machine location (left, center, right)
* 2. Set target location
* 3. Set tempo
*
* Machine position is a variable in arc_calc
*/

typedef struct {
    float launch_speed;
    float tilt_angle;
    float yaw_angle;
    float rpm_output;
    int target_location;
    int tempo;
} set_specs_t;

#define NUM_SETS 4

extern set_specs_t set_seq[NUM_SETS];

int set_machine_position(int position);

int choose_target_location(int target);
int choose_tempo(int tempo);

void advanced_save_set(int set_index, float launch_speed, float tilt_angle, float yaw_angle, float rpm_output);
void save_set(int set_index);

void common_sets();

void print_sets();

#endif // SET_H