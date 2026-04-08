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

#define WHEEL_R 0.1
#define EFF_K 0.9

#define NUM_TARGETS 5
#define NUM_TEMPOS 4
#define NUM_MACHINE_POSITIONS 3

#define M_PI 3.14159265358979323846

// machine position
extern int machine_position; // 0 for left, 1 for center, 2 for right?
extern float machine_x[NUM_MACHINE_POSITIONS]; // x-coordinates of target locations
extern const float machine_y; // y-coordinate of machine (fixed)

//target position
extern int target_position;
extern float target_x[NUM_TARGETS]; // x-coordinates of target locations
extern float target_y; // y-coordinate of target (fixed)

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

void yaw_calculation();

float landing_position(float xi, float yi, float theta, float rpm, float yf);

#endif //ARC_CALC_H