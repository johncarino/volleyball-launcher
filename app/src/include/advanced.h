#ifndef ADVANCED_H
#define ADVANCED_H

#include <stdint.h>
#include "arc_calc.h"

/*
* Advanced Mode Header
*
* Operations within Advanced Mode:
* 1. User selects a launch angle
* 2. User selects a launch speed
* 3. User tests launches the ball
* 4. User saves the launch parameter in a save slot
*
*/

void select_launch_angle(int angle);
void select_launch_speed(int speed);
void test_launch();
void save_launch_parameters(int slot);

#endif // ADVANCED_H