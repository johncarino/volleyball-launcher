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

    float angle = tilt_angle[mp][tl-1][t-1];
    if (angle > 90.0 || angle < 5.0) {
        fprintf(stderr, "Invalid tilt angle for set %d: %.2f degrees (must be between 5 and 90 degrees).\n", set_index, angle);
        return 0;
    }

    float rpm = rpm_output[mp][tl-1][t-1];
    if (rpm > 3000.0) {
        fprintf(stderr, "Invalid RPM output for set %d: %.2f (must be 3000 or less).\n", set_index, rpm);
        return 0;
    }

    set_seq[mp][set_index].launch_speed = launch_speed[mp][tl-1][t-1];
    set_seq[mp][set_index].tilt_angle = angle;
    set_seq[mp][set_index].yaw_angle = yaw_angle[mp][tl-1][t-1];
    set_seq[mp][set_index].rpm_output = rpm;
    set_seq[mp][set_index].target_location = tl;
    set_seq[mp][set_index].tempo = t;

    fprintf(stdout, "Set %d saved for machine position %d: Target Location %d, Tempo %d, Tilt Angle %.2f degrees, Yaw Angle %.2f degrees, Launch Speed %.2f m/s, RPM Output %.2f\n",
            set_index, mp, tl, t, angle, yaw_angle[mp][tl-1][t-1], launch_speed[mp][tl-1][t-1], rpm);

    return 1;
}

// Refreshes every already-saved set slot from the current tilt_angle/
// rpm_output/launch_speed/yaw_angle tables (see arc_calc.c). Those tables are
// recomputed whenever calibration changes (net height / court dimensions),
// but save_set() copies a snapshot into set_seq at save time, so previously
// saved sets go stale unless re-derived. Call this after calibration changes
// (e.g. once the user leaves the court settings tab) to bring every saved
// slot back in sync with the new calibration.
//
// An empty/never-saved slot is identified by target_location == 0 (the
// zero-initialized default; save_set() only ever stores 1-based locations),
// so those are skipped. Slots that fail validation under the new calibration
// (e.g. tilt angle or RPM now out of range) are left as-is with a warning,
// matching save_set()'s existing failure behavior, since there's no better
// value to fall back to without user input.
//
// Returns the number of slots successfully recalculated.
int recalculate_saved_sets(void) {
    int updated = 0;

    for (int mp = 0; mp < NUM_MACHINE_POSITIONS; mp++) {
        for (int set_index = 0; set_index < NUM_SETS; set_index++) {
            set_specs_t *slot = &set_seq[mp][set_index];

            if (slot->target_location == 0) {
                continue; // never saved
            }

            int tl = slot->target_location;
            int t = slot->tempo;

            if (save_set(set_index, mp, tl, t)) {
                updated++;
            } else {
                fprintf(stderr, "Warning: set %d (machine position %d) is no longer valid after the calibration change; it may need to be re-saved.\n", set_index, mp);
            }
        }
    }

    return updated;
}
