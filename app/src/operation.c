#include "operation.h"

float curr_tilt_angle = 0.0;
float curr_yaw_angle = 0.0;

float d_angle = 0;

//Defined in set.h
//#define TILT_COEFF 135.0 //171ms per degree, determined experimentally
//#define YAW_COEFF 10 //10 steps per degree, determined experimentally
//#define SPEED_COEFF 2.36

pthread_t tilt_thread, yaw_thread;
pthread_mutex_t tilt_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t yaw_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t done_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t tilt_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t yaw_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t done_cond = PTHREAD_COND_INITIALIZER;

volatile float tilt_angle_w = 0;
volatile float yaw_angle_w = 0;
volatile int tilt_done = 1;
volatile int yaw_done = 1;
volatile int shutdown = 0;

tb6600_t motor;
static mcp4725_t dac1 = MCP4725_INIT_ZERO;

void* tilt_worker() {
    while(true) {
        pthread_mutex_lock(&tilt_mutex);
        while(tilt_done && !shutdown) {
            pthread_cond_wait(&tilt_cond, &tilt_mutex);
        }
        if (shutdown) {
            pthread_mutex_unlock(&tilt_mutex);
            return NULL;
        }
        float work = tilt_angle_w;
        pthread_mutex_unlock(&tilt_mutex);

        tilt_signal(work);

        pthread_mutex_lock(&done_mutex);
        tilt_done = 1;
        pthread_cond_signal(&done_cond);
        pthread_mutex_unlock(&done_mutex);
    }
}

void* yaw_worker() {
    while(true) {
        pthread_mutex_lock(&yaw_mutex);
        while(yaw_done && !shutdown) {
            pthread_cond_wait(&yaw_cond, &yaw_mutex);
        }
        if (shutdown) {
            pthread_mutex_unlock(&yaw_mutex);
            return NULL;
        }
        float work = yaw_angle_w;
        pthread_mutex_unlock(&yaw_mutex);

        yaw_signal(work);

        pthread_mutex_lock(&done_mutex);
        yaw_done = 1;
        pthread_cond_signal(&done_cond);
        pthread_mutex_unlock(&done_mutex);
    }
}

void operation_init() {

    shutdown = 0;

    curr_tilt_angle = INITIAL_TILT_ANGLE;
    curr_yaw_angle = 0.0;

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

    //init threads
    if (pthread_create(&tilt_thread, NULL, tilt_worker, NULL) != 0) {
        fprintf(stderr, "Failed to create tilt thread\n");
        return;
    }
    if (pthread_create(&yaw_thread, NULL, yaw_worker, NULL) != 0) {
        fprintf(stderr, "Failed to create yaw thread\n");
        return;
    }
}

void operation_cleanup() {

    printf("First reset to default position...\n");
    mcp4725_set_raw(&dac1, 0);
    tilt_signal(INITIAL_TILT_ANGLE);

    printf("Cleaning up operation mode...\n");

    shutdown = 1;

    pthread_mutex_lock(&tilt_mutex);
    pthread_cond_signal(&tilt_cond);
    pthread_mutex_unlock(&tilt_mutex);

    pthread_mutex_lock(&yaw_mutex);
    pthread_cond_signal(&yaw_cond);
    pthread_mutex_unlock(&yaw_mutex);

    pthread_join(tilt_thread, NULL);
    pthread_join(yaw_thread, NULL);

    tb6600_enable(&motor, 0);
    tb6600_close(&motor);

    mcp4725_cleanup(&dac1);

    bts_cleanup();
}

void homing_sequence() {
    //home the machine to a known position
    //for now, just set tilt and yaw to 0
    printf("Homing sequence initiated. Moving to default position...\n");
    tilt_signal(INITIAL_TILT_ANGLE);
    yaw_signal(0.0);
    curr_tilt_angle = INITIAL_TILT_ANGLE;
    curr_yaw_angle = 0.0;
}

void tilt_signal(float angle) {

    if (angle > 81.0 || angle < INITIAL_TILT_ANGLE) {
        fprintf(stderr, "Invalid tilt angle: %.2f degrees (must be between %.2f and 81 degrees). Skipping tilt.\n", angle, INITIAL_TILT_ANGLE);
        return;
    }

    float delta_angle = angle - curr_tilt_angle;
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
        tilt_duration = tilt_angle_to_time(curr_tilt_angle, angle);
        printf("tilting forward by %.2f degrees to %.2f degrees for %ld ms\n", delta_angle, angle, tilt_duration);

        //(void)duty_cycle; // Avoid unused variable warning
        forward_ms(duty_cycle, tilt_duration);
    }
    else { // if delta_angle < 0
        delta_angle = -delta_angle;
        //convert delta_angle to tilt_duration
        tilt_duration = tilt_angle_to_time(curr_tilt_angle, angle);
        printf("tilting reverse by %.2f degrees to %.2f degrees for %ld ms\n", delta_angle, angle, tilt_duration);
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
        printf("No change in yaw angle\n");
        return;
    }
    else if (delta_angle > 0) {
        tb6600_set_direction(&motor, 1);
        //convert delta_angle to yaw steps
        yaw_steps = delta_angle * YAW_COEFF;
        printf("yaw right by %.2f degrees to %.2f degrees for %d steps\n", delta_angle, angle, yaw_steps);

        //(void)delay; // Avoid unused variable warning
        tb6600_step(&motor, yaw_steps, delay);
    }
    else { // if delta_angle < 0
        tb6600_set_direction(&motor, 0);
        delta_angle = -delta_angle;
        //convert delta_angle to yaw steps
        yaw_steps = delta_angle * YAW_COEFF;
        printf("yaw left by %.2f degrees to %.2f degrees for %d steps\n", delta_angle, angle, yaw_steps);
        tb6600_step(&motor, yaw_steps, delay);
    }

    curr_yaw_angle = angle;
}

void speed_signal(float speed) {
    if (speed > 1200.0) {
        fprintf(stderr, "Invalid RPM: %.2f (must be 1200 or less). Skipping speed.\n", speed);
        return;
    }
    uint16_t mv = 0;
    //convert speed to mv
    mv = rpm_to_mv(speed);
    //(void)speed;
    printf("setting speed to %.2f mV\n", (float)mv);
    mcp4725_set_mv(&dac1, mv);
}

void set_machine(int set_index) {
    //start the machine for set index
    
    //set rpm to 0
    mcp4725_set_raw(&dac1, 0);

    printf("Setting machine for set %d\n", set_index);
    printf("Tilt angle: %f, Yaw angle: %f, Speed: %f\n",
            set_seq[set_index].tilt_angle,
            set_seq[set_index].yaw_angle, 
            set_seq[set_index].rpm_output);
    
    //signal tilt
    pthread_mutex_lock(&tilt_mutex);
    tilt_angle_w = set_seq[set_index].tilt_angle;
    tilt_done = 0;
    pthread_cond_signal(&tilt_cond);
    pthread_mutex_unlock(&tilt_mutex);

    //signal yaw
    //pthread_mutex_lock(&yaw_mutex);
    //yaw_angle_w = set_seq[set_index].yaw_angle;
    //yaw_done = 0;
    //pthread_cond_signal(&yaw_cond);
    //pthread_mutex_unlock(&yaw_mutex);

    //wait for tilt and yaw to finish
    pthread_mutex_lock(&done_mutex);
    //while (!tilt_done || !yaw_done) {
    while (!tilt_done) {
        pthread_cond_wait(&done_cond, &done_mutex);
    }
    pthread_mutex_unlock(&done_mutex);

    //signal speed
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