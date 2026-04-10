#ifndef SET_H
#define SET_H

#include <stdint.h>
#include "arc_calc.h"

/*
* Set Mode Header
*
* Operations within Set Mode:
* 1. Define machine location (left, center, right)
* 2. Set target location
* 3. Set tempo
*
* Machine position is a variable in arc_calc
*/

typedef struct {
    float launch_speed;
    float tilt_angle;
    float yaw_angle;
    float rpm_output;
    int target_location;
    int tempo;
} set_specs_t;

#define NUM_SETS 4
//#define TILT_COEFF 150.0 //150ms per degree, determined experimentally
#define YAW_COEFF 10 //10 steps per degree, determined experimentally
#define SPEED_COEFF 2.36

//tilt coefficients for every 15 degrees (ms per degree)
//Foward coefficients
#define FTC_9_15 175.0
#define FTC_15_30 160.0
#define FTC_30_45 146.6666
#define FTC_45_60 134.6666
#define FTC_60_75 110.0
#define FTC_75_85 85.0

//Reverse coefficients
#define RTC_9_15 158.3
#define RTC_15_30 150.0
#define RTC_30_45 146.6666
#define RTC_45_60 133.3333
#define RTC_60_75 113.3333
#define RTC_75_85 100.0

extern set_specs_t set_seq[NUM_SETS];

uint16_t rpm_to_mv(float rpm);

long tilt_angle_to_time(float i_angle, float f_angle);

int set_machine_position(int position);

int choose_target_location(int target);
int choose_tempo(int tempo);

void advanced_save_set(int set_index, float launch_speed, float tilt_angle, float yaw_angle, float rpm_output);
int save_set(int set_index, int print_info);

void common_sets();

void print_sets();

#endif // SET_H