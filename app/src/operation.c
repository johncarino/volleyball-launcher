#include "operation.h"

float curr_tilt_angle = 0.0;
float curr_yaw_angle = 0.0;

float d_angle = 0;
float tilt_coeff = 120.0; //171ms per degree, determined experimentally
float yaw_coeff = 10; //10 steps per degree, determined experimentally
float speed_coeff = 1.875;

tb6600_t motor;
static mcp4725_t dac1 = MCP4725_INIT_ZERO;

void operation_init() {


    //init tb6600
    if (tb6600_init(&motor, 1) < 0) {
        fprintf(stderr, "Failed to initialize TB6600\n");
        return;
    }
    tb6600_enable(&motor, 1);

    //init mcp4725
    if (mcp4725_init(&dac1, MCP4725_I2C_BUS1, MCP4725_I2C_ADDR) != 0) {
        fprintf(stderr, "Failed to initialize MCP4725 — is I2C enabled?\n");
        return;
    }

    //init bts7960
    if (bts_init() != 0) {
        fprintf(stderr, "Failed to initialize BTS/BTN7960 HAL. Are you running as root?\n");
        return;
    }

    //set_machine(0);
}

void operation_cleanup() {
    printf("Cleaning up operation mode...\n");

    tb6600_enable(&motor, 0);
    tb6600_close(&motor);

    mcp4725_set_raw(&dac1, 0);
    mcp4725_cleanup(&dac1);

    bts_cleanup();
}

void homing_sequence() {
    //home the machine to a known position
    //for now, just set tilt and yaw to 0
    printf("Homing sequence initiated. Moving to default position...\n");
    tilt_signal(0.0);
    yaw_signal(0.0);
    curr_tilt_angle = 0.0;
    curr_yaw_angle = 0.0;
}

void tilt_signal(float angle) {
    //convert radians to degrees
    d_angle = angle * 180.0 / 3.14159;

    float delta_angle = d_angle - curr_tilt_angle;
    long tilt_duration = 0.0;
    //duty_cycle of linear actuator, as a percentage
    //keep this a constant
    int duty_cycle = 100;

    if (delta_angle == 0) {
        printf("No change in tilt angle\n");
        return;
    }
    else if (delta_angle > 0) {
        //convert delta_angle to tilt_duration
        tilt_duration = delta_angle * tilt_coeff;
        printf("tilting forward by %.2f degrees to %.2f degrees for %ld ms\n", delta_angle, d_angle, tilt_duration);

        //(void)duty_cycle; // Avoid unused variable warning
        forward_ms(duty_cycle, tilt_duration);
    }
    else { // if delta_angle < 0
        delta_angle = -delta_angle;
        //convert delta_angle to tilt_duration
        tilt_duration = delta_angle * tilt_coeff;
        printf("tilting reverse by %.2f degrees to %.2f degrees for %ld ms\n", delta_angle, d_angle, tilt_duration);
        reverse_ms(duty_cycle, tilt_duration);
    }

    curr_tilt_angle = d_angle;
}

void yaw_signal(float angle) {

    float delta_angle = angle - curr_yaw_angle;
    int yaw_steps = 0.0;
    //delay of stepper motor, in us
    //keep this a constant
    int delay = 500;

    if (delta_angle == 0) {
        printf("No change in yaw angle\n");
        return;
    }
    else if (delta_angle > 0) {
        tb6600_set_direction(&motor, 1);
        //convert delta_angle to yaw steps
        yaw_steps = delta_angle * yaw_coeff;
        printf("yaw right by %.2f degrees to %.2f degrees for %d steps\n", delta_angle, angle, yaw_steps);

        //(void)delay; // Avoid unused variable warning
        tb6600_step(&motor, yaw_steps, delay);
    }
    else { // if delta_angle < 0
        tb6600_set_direction(&motor, 0);
        delta_angle = -delta_angle;
        //convert delta_angle to yaw steps
        yaw_steps = delta_angle * yaw_coeff;
        printf("yaw left by %.2f degrees to %.2f degrees for %d steps\n", delta_angle, angle, yaw_steps);
        tb6600_step(&motor, yaw_steps, delay);
    }

    curr_yaw_angle = angle;
}

void speed_signal(float speed) {
    uint16_t mv = 0;
    //convert speed to mv
    mv = (uint16_t)(speed * speed_coeff);
    //(void)speed;
    printf("setting speed to %.2f mV\n", (float)mv);
    mcp4725_set_mv(&dac1, mv);
}
void set_machine(int set_index) {
    //start the machine for set index
    //run in parallel?

    //testing with comments
    printf("Setting machine for set %d\n", set_index);
    printf("Tilt angle: %f, Yaw angle: %f, Speed: %f\n", set_seq[set_index].tilt_angle, set_seq[set_index].yaw_angle, set_seq[set_index].rpm_output);
    tilt_signal(set_seq[set_index].tilt_angle);
    yaw_signal(set_seq[set_index].yaw_angle);

    //run after aiming is done
    speed_signal(set_seq[set_index].rpm_output);
}

/*
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
*/