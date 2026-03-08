#ifndef SET_H
#define SET_H

#include <stdint.h>
#include "arc_calc.h"

/*
* Set Mode Header
*
* Ooperations within Set Mode:
* 1. Define machine location (left, center, right)
* 2. Set target location
* 3. Set tempo
*
* Machine position is a variable in arc_calc
*/

void set_machine_position(int position);

void choose_target_location(int target);
void choose_tempo(int tempo);

#endif // SET_H