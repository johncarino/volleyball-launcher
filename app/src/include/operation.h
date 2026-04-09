#ifndef OPERATION_H
#define OPERATION_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "set.h"
#include "hal/bts7960.h"
#include "hal/mcp4725.h"
#include "hal/tb6600.h"

/*
* Operation Mode Header
*
* Operations within Operation Mode:
* 1. Launch the ball based on entries in the set sequence
* 2. User can stop the machine at any time
* 3. User can repeat the same set
* 4. User can shuffle the set sequence
*/

#define INITIAL_TILT_ANGLE 7.0

void operation_init();
void operation_cleanup();
void homing_sequence();

void tilt_signal(float angle);
void yaw_signal(float angle);
void speed_signal(float speed);
void set_machine(int set_index);
void machine_operating();
void stop_machine();
void repeat_set();
void shuffle_set_sequence();

#endif // OPERATION_H