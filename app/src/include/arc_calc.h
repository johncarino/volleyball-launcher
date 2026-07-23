#ifndef ARC_CALC_H
#define ARC_CALC_H

#include <stdint.h>
#include <stdio.h>
#include <math.h>

/*
* Arc Calculation Header
*
* Calculates all predefined sets based on the given
*
* inputs:
* - net height
* - court dimensions
* - machine position
*
* outputs:
* - matrix of tilt angles (in volts)
* - matrix of yaw angles (in volts)
*/

//GLOBAL CONSTANTS
#define GRAVITY 9.81

#define WHEEL_R 0.075
#define EFF_K 0.8

#define NUM_TARGETS 4
#define NUM_TEMPOS 5
#define NUM_MACHINE_POSITIONS 3

#define M_PI 3.14159265358979323846

// --- Tilt-pivot / ball-exit geometry ---
// machine_x/machine_y are the fixed machine reference point (e.g. where the
// machine is placed/tracked on the court) - NOT the tilt pivot itself. There
// are two stacked offsets between that reference point and where the ball
// actually leaves the machine. All x offsets below are negative: the pivot
// and exit point sit BEHIND the machine reference point (toward negative x),
// not ahead of it.
//
// 1) Reference point -> tilt pivot: a fixed offset that does NOT change with
//    tilt angle (the pivot's mounting location is rigid relative to the
//    machine reference point).
//    Measured: x offset = -458mm, y offset = 0mm.
//
// 2) Tilt pivot -> ball exit point: changes with tilt_angle, since the wheels
//    are carried by the tilting assembly. Measured offset of the exit point
//    from the pivot (metres), by tilt angle:
//      theta=0  deg: dx=-0.290, dy=0.274
//      theta=45 deg: dx=-0.520, dy=0.300
//      theta=90 deg: dx=-0.705, dy=0.164
//    The magnitude of this offset is NOT constant with theta (0.399 -> 0.600
//    -> 0.724 m), so the exit point is not simply a fixed-length arm rotating
//    about the pivot (the real mechanism is more complex, e.g. a linkage).
//    Rather than model the linkage, dx_off/dy_off are fit with an exact
//    quadratic through the 3 measured points above (valid for theta in
//    [0, 90] deg - the normal tilt range). Re-derive these coefficients if
//    the mechanism changes or more calibration points are measured.
//
// exit_point_offset() returns the TOTAL offset (1 + 2 combined) from
// machine_x/machine_y to the ball exit point.
#define PIVOT_OFFSET_X (-0.458f)
#define PIVOT_OFFSET_Y 0.0f

#define ARM_DX_C2 (0.0000111111f)
#define ARM_DX_C1 (-0.0056111111f)
#define ARM_DX_C0 (-0.2900000000f)

#define ARM_DY_C2 (-0.0000400000f)
#define ARM_DY_C1 (0.0023777778f)
#define ARM_DY_C0 (0.2740000000f)

// number of fixed-point iterations used to solve for tilt_angle while
// accounting for the exit-point offset (theta affects the exit point, which
// in turn affects theta) - converges in just a few passes.
#define ARM_OFFSET_ITERATIONS 8

// machine position
extern int machine_position; // 0 for left, 1 for center, 2 for right?
extern float machine_x[NUM_MACHINE_POSITIONS]; // x-coordinates of target locations
extern const float machine_y; // y-coordinate of machine (fixed)

//target position
extern int target_position;
extern float target_x[NUM_TARGETS]; // x-coordinates of target locations
extern const float target_y; // y-coordinate of target (fixed)

//peak heights
extern float peak_height[NUM_TEMPOS];

extern float tilt_angle[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];
extern float tilt_output[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];

extern float yaw_angle[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];
extern float yaw_output[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];

extern float launch_speed[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];
extern float launch_output[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];

extern float rpm_output[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];

void arc_calc_params(float net_height, float court_width, float court_length);

void calculation();

// Computes the (dx_off, dy_off) offset of the ball exit point relative to the
// tilt pivot, for a given tilt angle (degrees). dx_off is along the
// machine's forward-facing direction (toward whichever target it's yawed to
// face), NOT a fixed world-frame axis - dy_off is vertical (added to pivot
// height, unaffected by yaw).
void exit_point_offset(float theta_deg, float *dx_off, float *dy_off);

// Simulates the ball's landing x-position given a launch from world
// coordinates (xi, yi) at the given tilt angle/rpm. facing_dir must be +1.0
// if the machine is yawed to face the +x direction (target at higher x than
// xi), or -1.0 if it's yawed to face -x (target at lower x than xi) - this is
// needed because the exit-point offset and horizontal velocity are computed
// in the machine's forward-facing frame and must be rotated into world
// coordinates using this sign.
float landing_position(float xi, float yi, float theta, float rpm, float yf, float facing_dir);

#endif //ARC_CALC_H