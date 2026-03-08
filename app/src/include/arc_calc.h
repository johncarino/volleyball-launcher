#ifndef ARC_CALC_H
#define ARC_CALC_H

#include <stdint.h>

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

const int NUM_TARGETS = 3;
const int NUM_TEMPOS = 3;

int NUM_CALCS = NUM_TARGETS * NUM_TEMPOS;

float tilt_angle[NUM_CALCS];
float tilt_output[NUM_CALCS];

float yaw_angle[NUM_CALCS];
float yaw_output[NUM_CALCS];

void calculation(float net_height, float court_width, float court_length);


#endif //ARC_CALC_H