#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <stdint.h>

#include "arc_calc.h"

/*
* Calibration Header
* 
* Variables to be defined:
* - net height
* - court dimensions
*/

const float STANDARD_NET_HEIGHT = 2.43; // Standard volleyball net height in meters
const float STANDARD_COURT_LENGTH = 18.0; // Standard volleyball court length in meters
const float STANDARD_COURT_WIDTH = 9.0; // Standard volleyball court width

const float MIN_NET_HEIGHT = 1.0; // Minimum net height in meters
const float MAX_NET_HEIGHT = 3.0; // Maximum net height in meters

const float MIN_COURT_LENGTH = 10.0; // Minimum court length in meters
const float MAX_COURT_LENGTH = 30.0; // Maximum court length in meters

const float MIN_COURT_WIDTH = 5.0; // Minimum court width in meters
const float MAX_COURT_WIDTH = 20.0; // Maximum court width in meters

float net_height = STANDARD_NET_HEIGHT;
float court_length = STANDARD_COURT_LENGTH;
float court_width = STANDARD_COURT_WIDTH;

//Calibration (setters)
void set_net_height(float height);
void set_court_dimensions(float length, float width);

void calibrate_user_input(int input, float value);

//Getters
float get_net_height();
float get_court_length();
float get_court_width();

#endif // CALIBRATION_H