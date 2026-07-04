#ifndef OPERATION_H
#define OPERATION_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "set.h"
#include "bts7960.h"
#include "mcp4725.h"
#include "tb6600.h"
#include "mpu6050.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
* Operation Mode Header
*
* Operations within Operation Mode:
* 1. Launch the ball based on entries in the set sequence
* 2. User can stop the machine at any time
* 3. User can repeat the same set
* 4. User can shuffle the set sequence
*/

#define INITIAL_TILT_ANGLE 5.0

void operation_init();
void operation_cleanup();
void homing_sequence();

void tilt_signal(float angle);
void yaw_signal(float angle);
void speed_signal(float speed);
void set_speed(float speed);
void set_raw_speed(float mv);
void percentage_to_mv(float percentage);
void set_machine(int set_index);
void tilt_signal_advanced(float angle);
void yaw_signal_advanced(float angle);
void tilt_with_feedback(float angle);
void toggle_hopper();
void hopper_start();
void hopper_stop();
void hopper_pulse();

float get_tilt_angle();
float get_yaw_angle();
int get_speed();
int get_rpm();

void machine_operating();
void resume_machine();
void pause_machine();
void repeat_set();
void shuffle_set_sequence();

#ifdef __cplusplus
}
#endif

#endif // OPERATION_H