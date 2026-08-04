#ifndef ARC_CALC_H
#define ARC_CALC_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Arc Calculation Header
 *
 * Calculates all predefined launch trajectories based on:
 *
 * net height
 * court dimensions
 * machine position
 * target position
 * tempo height
 *
 * The calculation produces:
 *
 * tilt angle
 * launch speed
 * wheel revolutions per minute
 */


/*
 * Physical constants
 */

#define GRAVITY 9.81f

#define WHEEL_R 0.075f
#define EFF_K 0.7f

#define ARC_PI 3.14159265358979323846f

#define PEAK_HEIGHT_OFFSET 0.35f


/*
 * Array sizes
 */

#define NUM_TARGETS 5
#define NUM_TEMPOS 4
#define NUM_MACHINE_POSITIONS 3


/*
 * Tilt pivot and ball exit geometry
 *
 * All dimensions are in metres.
 *
 * PIVOT_OFFSET_X and PIVOT_OFFSET_Y represent the fixed offset from the
 * machine reference point to the tilt pivot.
 */

#define PIVOT_OFFSET_X (-0.44f)
#define PIVOT_OFFSET_Y 0.0f


/*
 * ARM_LEN_A and ARM_LEN_B represent the rotating vector from the tilt pivot
 * to the ball exit point.
 */

#define ARM_LEN_A 0.15576f
#define ARM_LEN_B 0.28977f


/*
 * Number of fixed point iterations used when solving for the launch angle.
 */

#define ARM_OFFSET_ITERATIONS 8


/*
 * Machine position
 *
 * machine_position:
 *
 * 0 means left
 * 1 means centre
 * 2 means right
 */

extern int machine_position;

extern float machine_x[NUM_MACHINE_POSITIONS];

extern const float machine_y;


/*
 * Target position
 */

extern int target_position;

extern float target_x[NUM_TARGETS];

extern const float target_y;


/*
 * Selected tempo
 */

extern int tempo_position;

extern float peak_height[NUM_TEMPOS];


/*
 * Calculated tilt values
 */

extern float tilt_angle
    [NUM_MACHINE_POSITIONS]
    [NUM_TARGETS]
    [NUM_TEMPOS];

extern float tilt_output
    [NUM_MACHINE_POSITIONS]
    [NUM_TARGETS]
    [NUM_TEMPOS];


/*
 * Calculated yaw values
 */

extern float yaw_angle
    [NUM_MACHINE_POSITIONS]
    [NUM_TARGETS]
    [NUM_TEMPOS];

extern float yaw_output
    [NUM_MACHINE_POSITIONS]
    [NUM_TARGETS]
    [NUM_TEMPOS];


/*
 * Calculated launch speed values
 */

extern float launch_speed
    [NUM_MACHINE_POSITIONS]
    [NUM_TARGETS]
    [NUM_TEMPOS];

extern float launch_output
    [NUM_MACHINE_POSITIONS]
    [NUM_TARGETS]
    [NUM_TEMPOS];


/*
 * Calculated wheel speeds
 */

extern float rpm_output
    [NUM_MACHINE_POSITIONS]
    [NUM_TARGETS]
    [NUM_TEMPOS];


/*
 * Updates all court dimensions and runs the trajectory calculations.
 *
 * All distance values must be supplied in metres.
 */
void arc_calc_params(
    float net_height,
    float court_width,
    float court_length
);


/*
 * Calculates all trajectory combinations.
 *
 * arc_calc_params should normally be called instead of calling this function
 * directly because arc_calc_params initializes the required arrays first.
 */
void calculation(void);


/*
 * Calculates the total offset from the machine reference point to the ball
 * exit point.
 *
 * theta_deg is the tilt angle in degrees.
 *
 * dx_off is measured in the machine forward direction.
 *
 * dy_off is measured vertically.
 */
void exit_point_offset(
    float theta_deg,
    float *dx_off,
    float *dy_off
);


/*
 * Calculates the world x coordinate where the ball reaches height yf.
 *
 * facing_dir must be:
 *
 * positive when the machine faces increasing world x
 * negative when the machine faces decreasing world x
 */
float landing_position(
    float xi,
    float yi,
    float theta,
    float rpm,
    float yf,
    float facing_dir
);

#endif