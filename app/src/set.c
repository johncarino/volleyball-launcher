#include "set.h"

set_specs_t set_seq[NUM_MACHINE_POSITIONS][NUM_SETS];

int save_set(int set_index, int mp, int tl, int t) {
    if (set_index < 0 || set_index >= NUM_SETS) {
        fprintf(stderr, "Invalid set index. Must be between 0 and %d.\n", NUM_SETS - 1);
        return 0;
    }

    if (mp < 0 || mp >= NUM_MACHINE_POSITIONS) {
        fprintf(stderr, "Invalid machine position. Must be between 0 and %d.\n", NUM_MACHINE_POSITIONS - 1);
        return 0;
    }

    if (tl < 1 || tl > NUM_TARGETS) {
        fprintf(stderr, "Invalid target location. Must be between 1 and %d.\n", NUM_TARGETS);
        return 0;
    }

    if (t < 1 || t > NUM_TEMPOS) {
        fprintf(stderr, "Invalid tempo. Must be between 1 and %d.\n", NUM_TEMPOS);
        return 0;
    }

    float angle = tilt_angle[mp][tl][t];
    if (angle > 81.0 || angle < 9.0) {
        fprintf(stderr, "Invalid tilt angle for set %d: %.2f degrees (must be between 9 and 81 degrees).\n", set_index, angle);
        return 0;
    }

    float rpm = rpm_output[mp][tl][t];
    if (rpm > 2000.0) {
        fprintf(stderr, "Invalid RPM output for set %d: %.2f (must be 2000 or less).\n", set_index, rpm);
        return 0;
    }

    set_seq[mp][set_index].launch_speed = launch_speed[mp][tl][t];
    set_seq[mp][set_index].tilt_angle = angle;
    set_seq[mp][set_index].yaw_angle = yaw_angle[mp][tl][t];
    set_seq[mp][set_index].rpm_output = rpm;
    set_seq[mp][set_index].target_location = tl;
    set_seq[mp][set_index].tempo = t;

    return 1;
}