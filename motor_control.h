#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

/**
 * Set the launch-wheel motor speed.
 * @param speed  0–100 (percent duty cycle)
 */
void set_motor_speed(int speed);

/**
 * Set the launcher elevation angle.
 * @param angle  0–90 (degrees)
 */
void set_motor_angle(int angle);

/**
 * Emergency-stop all motors immediately.
 */
void stop_motor(void);

#endif /* MOTOR_CONTROL_H */
