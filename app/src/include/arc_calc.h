#ifndef ARC_CALC_H
#define ARC_CALC_H

#include <stdint.h>
#include <cmath>

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
const float GRAVITY = 9.81;

const int wheel_r = 0.1;
const int eff_k = 0.9;

const int NUM_TARGETS = 5;
const int NUM_TEMPOS = 4;
const int NUM_MACHINE_POSITIONS = 3;

// machine position
int machine_position = 0; // 0 for left, 1 for center, 2 for right?
float machine_x[NUM_MACHINE_POSITIONS]; // x-coordinates of target locations
const float machine_y = 1.75; // y-coordinate of machine (fixed)

//target position
int target_position = 0;
float target_x[NUM_TARGETS]; // x-coordinates of target locations
const float target_y = 0.0; // y-coordinate of target (fixed)

//peak heights
float peak_height[NUM_TEMPOS];

float tilt_angle[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];
float tilt_output[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];

float yaw_angle[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];
float yaw_output[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];

float launch_speed[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];
float launch_output[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];

float rpm_output[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];

void arc_calc_init();

void calculation(float net_height, float court_width, float court_length);

float landing_position(float xi, float yi, float theta, float rpm, float yf);

#endif //ARC_CALC_H