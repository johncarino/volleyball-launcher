#include "operation.h"

float curr_tilt_angle = 0.0;
float curr_yaw_angle = 0.0;

float tilt_coeff = 1;
float yaw_coeff = 1;

void operation_init() {


    //init tb6600
    if (tb6600_init(&motor, 1) < 0) {
        fprintf(stderr, "Failed to initialize TB6600\n");
        return;
    }
    tb6600_enable(&motor, 1);

    //init mcp4725
    if (mcp4725_init(MCP4725_I2C_BUS, MCP4725_I2C_ADDR) != 0) {
        fprintf(stderr, "Failed to initialize MCP4725 — is I2C enabled?\n");
        return -1;
    }

    set_machine(0);
}
void tilt_signal(float angle) {
    float delta_angle = angle - curr_tilt_angle;
    long tilt_duration = 0.0;
    //duty_cycle of linear actuator, as a percentage
    //keep this a constant
    int duty_cycle = 80;

    if (delta_angle == 0) {
        //no change
        return;
    }
    else if (delta_angle > 0) {
        //convert delta_angle to tilt_duration
        //tilt_duration = delta_angle * tilt_coeff;
        forward_ms(duty_cycle, tilt_duration);
    }
    else { // if delta_angle < 0
        delta_angle = -delta_angle;
        //convert delta_angle to tilt_duration
        //tilt_duration = delta_angle * tilt_coeff;
        reverse_ms(duty_cycle, tilt_duration);
    }

    curr_tilt_angle = angle;
}

void yaw_signal(float angle) {
    float delta_angle = angle - curr_yaw_angle;
    int yaw_steps = 0.0;
    //delay of stepper motor, in us
    //keep this a constant
    int delay = 500;

    if (delta_angle == 0) {
        //no change
        return;
    }
    else if (delta_angle > 0) {
        tb6600_set_direction(&motor, 1);
        //convert delta_angle to yaw steps
        //yaw_steps = delta_angle * yaw_coeff;
        tb6600_step(&motor, yaw_steps, delay);
    }
    else { // if delta_angle < 0
        tb6600_set_direction(&motor, 0);
        delta_angle = -delta_angle;
        //convert delta_angle to yaw steps
        //yaw_steps = delta_angle * yaw_coeff;
        tb6600_step(&motor, yaw_steps, delay);
    }

    curr_yaw_angle = angle;
}

void speed_signal(float speed) {
    uint16_t mv = 0;
    //convert speed to mv
    mcp4725_set_mv(mv);
}
void set_machine(int set_index) {
    //start the machine for set index
    //run in parallel?
    tilt_signal(set_seq[set_index]->tilt_angle);
    yaw_signal(set_seq[set_index]->yaw_angle);

    //run after aiming is done
    speed_signal(set_seq[set_index]->rpm_output);
}

void machine_operating() {
    int curr_set_idx = 1;
    while(stop isnt invoked) {
        if (set once) {
            if (repeat_set) {
                repeat_set
            }
            else {
                set_machine(curr_set_idx);
                curr_set_idx = curr_set_idx++ % NUM_SETS;
            }
        }

        if (shuffle sequence) {
            shuffle set set sequence once
        }
    }
}