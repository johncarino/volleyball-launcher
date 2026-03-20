#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <stdint.h>
#include <stdio.h>

#include "arc_calc.h"

/*
* Calibration Header
* 
* Variables to be defined:
* - net height
* - court dimensions
*/

extern const float STANDARD_NET_HEIGHT; // Standard volleyball net height in meters
extern const float STANDARD_COURT_LENGTH; // Standard volleyball court length in meters
extern const float STANDARD_COURT_WIDTH; // Standard volleyball court width

extern const float MIN_NET_HEIGHT; // Minimum net height in meters
extern const float MAX_NET_HEIGHT; // Maximum net height in meters

extern const float MIN_COURT_LENGTH; // Minimum court length in meters
extern const float MAX_COURT_LENGTH; // Maximum court length in meters

extern const float MIN_COURT_WIDTH; // Minimum court width in meters
extern const float MAX_COURT_WIDTH; // Maximum court width in meters

extern float net_height;
extern float court_length;
extern float court_width;

//Calibration (setters)
void set_net_height(float height);
void set_court_dimensions(float length, float width);

void calibrate_user_input(char input, float value);

//Getters
float get_net_height();
float get_court_length();
float get_court_width();

#endif // CALIBRATION_H