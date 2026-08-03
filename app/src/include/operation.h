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
#include "tachometer.h"
#include "hcsr04.h"
#include "buzzer.h"

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

#define INITIAL_TILT_ANGLE 8.0

void operation_init();
void operation_cleanup();
void homing_sequence();

void tilt_signal(float angle);
void yaw_signal(float angle);
void speed_signal(float speed);
void set_speed(float speed);
void speed_with_feedback(float rpm);
void set_raw_speed(float mv);
void percentage_to_mv(float percentage);
void set_machine(int machine_position, int set_index);
void tilt_signal_advanced(float angle);
void yaw_signal_advanced(float angle);
void tilt_with_feedback(float angle);
void toggle_hopper();
int hopper_start();
void hopper_stop();
int hopper_pulse();
void hopper_reset();
void hopper_mark_misaligned(void);
int hopper_needs_homing(void);
int get_tach_reading();

float get_tilt_angle();
float get_yaw_angle();
int get_speed();
int get_rpm();

/* Number of hopper pulses since the last re-home (mirrors the counter that
 * triggers a reset every HOPPER_RESET_INTERVAL_PULSES). */
int get_hopper_pulse_count();

/* Per-HAL-component init results as a bitmask (see COMPONENT_* below); a set
 * bit means that component's *_init() succeeded during operation_init(). */
#define COMPONENT_TACHOMETER     (1u << 0)
#define COMPONENT_IMU            (1u << 1)
#define COMPONENT_TILT_DRIVER    (1u << 2)
#define COMPONENT_HOPPER_STEPPER (1u << 3)
#define COMPONENT_FLYWHEEL_DAC   (1u << 4)
#define COMPONENT_BALL_SENSOR    (1u << 5)
int operation_component_status();

void machine_operating();
void resume_machine();
void pause_machine();
void repeat_set();
void shuffle_set_sequence();

/*
* Software interrupt (emergency abort) support.
*
* Raising the interrupt — via SIGINT/SIGUSR1 delivered to the process,
* or by calling operation_request_interrupt() directly — asks any
* in-progress blocking operation in operation.c (tilt/speed feedback
* loops, hopper stepping, etc.) to abort as soon as possible and leave
* the motors in a safe, stopped state.
*/
void operation_install_interrupt_handler(void);
void operation_request_interrupt(void);
void operation_force_stop(void);
int  operation_interrupt_pending(void);
void operation_clear_interrupt(void);

/* Feedback-sensor fault reporting for asynchronous native workers. */
void operation_clear_feedback_fault(void);
const char *operation_feedback_fault_message(void);

#ifdef __cplusplus
}
#endif

#endif // OPERATION_H
