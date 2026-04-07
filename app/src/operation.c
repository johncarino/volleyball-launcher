#include "operation.h"

float curr_tilt_angle = 0.0;
float curr_yaw_angle = 0.0;

float tilt_coeff = 1;
float yaw_coeff = 1;

pthread_t tilt_thread, yaw_thread;
pthread_mutex_t tilt_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t yaw_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t done_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t tilt_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t yaw_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t done_cond = PTHREAD_COND_INITIALIZER;
volatile int tilt_angle_work = 0;
volatile int yaw_angle_work = 0;
volatile int tilt_done = 1, yaw_done = 1;
volatile int shutdown_flag = 0;

tb6600_t motor;

static mcp4725_t dac1 = MCP4725_INIT_ZERO;  // Motor 1 – I2C bus 1

void* tilt_worker(void* arg) {
    (void)arg;
    while(1) {
        pthread_mutex_lock(&tilt_mutex);
        while(tilt_done && !shutdown_flag)
            pthread_cond_wait(&tilt_cond, &tilt_mutex);
        if (shutdown_flag) {
            pthread_mutex_unlock(&tilt_mutex);
            return NULL;
        }
        float work = tilt_angle_work;
        pthread_mutex_unlock(&tilt_mutex);

        tilt_signal(work);

        pthread_mutex_lock(&done_mutex);
        tilt_done = 1;
        pthread_cond_signal(&done_cond);
        pthread_mutex_unlock(&done_mutex);
    }
    return NULL;
}

void* yaw_worker(void* arg) {
    (void)arg;
    while(1) {
        pthread_mutex_lock(&yaw_mutex);
        while(yaw_done && !shutdown_flag)
            pthread_cond_wait(&yaw_cond, &yaw_mutex);
        if (shutdown_flag) {
            pthread_mutex_unlock(&yaw_mutex);
            return NULL;
        }
        float work = yaw_angle_work;
        pthread_mutex_unlock(&yaw_mutex);

        yaw_signal(work);

        pthread_mutex_lock(&done_mutex);
        yaw_done = 1;
        pthread_cond_signal(&done_cond);
        pthread_mutex_unlock(&done_mutex);
    }
    return NULL;
}

void operation_init() {
    shutdown_flag = 0;

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

    //create threads
    pthread_create(&tilt_thread, NULL, tilt_worker, NULL);
    pthread_create(&yaw_thread, NULL, yaw_worker, NULL);

}

void operation_cleanup() {
    //signal threads to shut down
    shutdown_flag = 1;

    pthread_mutex_lock(&tilt_mutex);
    pthread_cond_signal(&tilt_cond);
    pthread_mutex_unlock(&tilt_mutex);

    pthread_mutex_lock(&yaw_mutex);
    pthread_cond_signal(&yaw_cond);
    pthread_mutex_unlock(&yaw_mutex);

    //wait for threads to exit
    pthread_join(tilt_thread, NULL);
    pthread_join(yaw_thread, NULL);

    //cleanup tb6600
    tb6600_enable(&motor, 0);
    tb6600_close(&motor);

    //cleanup mcp4725
    mcp4725_set_mv(&dac1, 0); // Set throttle to 0 mV
    mcp4725_cleanup(&dac1);

    bts_cleanup();
}

void tilt_signal(float angle) {
    printf("start tilting\n");

    //convert radians to degrees
    angle = angle * 180 / M_PI;

    float delta_angle = angle - curr_tilt_angle;
    long tilt_duration = 0.0;
    //duty_cycle of linear actuator, as a percentage
    //keep this a constant
    int duty_cycle = 80;

    if (delta_angle == 0) {
        //no change
        //return;
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
    printf("done tilting\n");
}

void yaw_signal(float angle) {
    printf("start yawing\n");
    float delta_angle = angle - curr_yaw_angle;
    int yaw_steps = 0.0;
    //delay of stepper motor, in us
    //keep this a constant
    int delay = 500;

    if (delta_angle == 0) {
        //no change
        //return;
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
    printf("done yawing\n");
}

void speed_signal(float speed) {
    printf("setting speed\n");
    uint16_t mv = 0;
    //convert speed to mv
    (void)speed; // Placeholder to avoid unused parameter warning
    //mv = speed * speed_coeff;
    mcp4725_set_mv(&dac1, mv);
}

void set_machine(int set_index) {
    printf("Setting machine for set %d\n", set_index);
    printf("Tilt angle: %f, Yaw angle: %f, Speed: %f\n", 
           set_seq[set_index].tilt_angle, 
           set_seq[set_index].yaw_angle, 
           set_seq[set_index].rpm_output);

    // Signal tilt worker
    pthread_mutex_lock(&tilt_mutex);
    tilt_angle_work = set_seq[set_index].tilt_angle;
    tilt_done = 0;
    pthread_cond_signal(&tilt_cond);
    pthread_mutex_unlock(&tilt_mutex);

    // Signal yaw worker
    pthread_mutex_lock(&yaw_mutex);
    yaw_angle_work = set_seq[set_index].yaw_angle;
    yaw_done = 0;
    pthread_cond_signal(&yaw_cond);
    pthread_mutex_unlock(&yaw_mutex);

    // Wait for both to complete
    pthread_mutex_lock(&done_mutex);
    while(!tilt_done || !yaw_done) {
        pthread_cond_wait(&done_cond, &done_mutex);
    }
    pthread_mutex_unlock(&done_mutex);

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