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
extern float curr_tilt_angle;
extern float curr_yaw_angle;

tb6600_t motor;

//communicates with the hal files to send signal to the machine
void tilt_signal(float angle);
void yaw_signal(float angle);
void speed_signal(float speed);

void set_machine(int set_index);

void operation_init();
void stop_machine();
void repeat_set();
void shuffle_set_sequence();

void machine_operating();

#endif // OPERATION_H