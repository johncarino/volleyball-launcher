#include <stdint.h>
#include <stdio.h>
#include "motor_control.h"

void set_motor_speed(int speed) {
    // Placeholder: clamp to 0-100 and drive PWM
    if (speed < 0)   speed = 0;
    if (speed > 100) speed = 100;
    printf("Setting motor speed to %d%%\n", speed);
    // TODO: write PWM duty-cycle to hardware (e.g. /dev/pwm or I2C DAC)
}

void set_motor_angle(int angle) {
    // Placeholder: clamp to 0-90 and drive servo
    if (angle < 0)  angle = 0;
    if (angle > 90) angle = 90;
    printf("Setting launch angle to %d deg\n", angle);
    // TODO: write servo position to hardware (e.g. PWM channel 2)
}

void stop_motor(void) {
    // Placeholder: drive all motor outputs to zero immediately
    printf("Stopping all motors\n");
    // TODO: assert hardware brake / zero all PWM outputs
}