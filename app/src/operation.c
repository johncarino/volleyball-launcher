#include "operation.h"
#include <gpiod.h>

float curr_tilt_angle = 0.0;
float curr_yaw_angle = 0.0;

float d_angle = 0;

//Defined in set.h
//#define TILT_COEFF 135.0 //171ms per degree, determined experimentally
//#define YAW_COEFF 10 //10 steps per degree, determined experimentally
//#define SPEED_COEFF 2.36

#define TILT_TOLERANCE_DEG 0.5
#define TILT_TIMEOUT_SEC 10
#define TILT_LOOP_DELAY_US 50000

// === TACHOMETER ADDITION ===
// A3144 hall sensor on a GPIO input, wired per the earlier wiring
// notes (5V supply, 10k pull-up to 3.3V on OUT, OUT to a free GPIO
// line). These three should really live in set.h next to the other
// experimentally-determined coefficients, left here as defaults so
// this file still compiles standalone.
#ifndef TACH_GPIOCHIP
#define TACH_GPIOCHIP 2          // gpiochip number, find with gpiodetect
#endif
#ifndef TACH_LINE
#define TACH_LINE 7              // line offset, find with gpioinfo
#endif
#ifndef TACH_PULSES_PER_REV
#define TACH_PULSES_PER_REV 1    // magnets per revolution on the BLDC
#endif
#define TACH_DEBOUNCE_US 1000
#define TACH_STALE_TIMEOUT_SEC 2
#define TACH_AVG_WINDOW 5
// === END TACHOMETER ADDITION ===

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

// === TACHOMETER ADDITION ===
pthread_t tach_thread;
pthread_mutex_t tach_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile int tach_running = 0;

static struct gpiod_chip *tach_chip = NULL;
static struct gpiod_line *tach_line = NULL;

static double tach_periods[TACH_AVG_WINDOW];
static int tach_period_count = 0;
static int tach_period_idx = 0;
static double tach_current_rpm = 0.0;
// === END TACHOMETER ADDITION ===

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

// === TACHOMETER ADDITION ===
// Runs continuously for the life of the program (unlike the tilt/yaw
// workers, which only do work when signalled). Watches the A3144 line
// for falling edges, keeps a rolling average over TACH_AVG_WINDOW
// pulses, prints RPM to the terminal on every pulse, and prints 0 if
// no pulse arrives for TACH_STALE_TIMEOUT_SEC seconds (motor stopped).
void* tach_worker(void* arg) {
    (void)arg;

    struct timespec last_pulse_time;
    int have_last = 0;

    while (!shutdown) {
        struct timespec timeout = { TACH_STALE_TIMEOUT_SEC, 0 };
        int wait_result = gpiod_line_event_wait(tach_line, &timeout);

        if (wait_result < 0) {
            fprintf(stderr, "Tachometer: gpiod_line_event_wait failed\n");
            break;
        }
        if (wait_result == 0) {
            // No pulse within timeout: motor is likely stopped.
            if (have_last) {
                pthread_mutex_lock(&tach_mutex);
                tach_current_rpm = 0.0;
                tach_period_count = 0;
                tach_period_idx = 0;
                pthread_mutex_unlock(&tach_mutex);

                printf("Tachometer: RPM = 0.0 (no pulses for %ds)\n",
                       TACH_STALE_TIMEOUT_SEC);
                have_last = 0;
            }
            continue;
        }

        struct gpiod_line_event event;
        if (gpiod_line_event_read(tach_line, &event) < 0) {
            fprintf(stderr, "Tachometer: gpiod_line_event_read failed\n");
            break;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        if (!have_last) {
            last_pulse_time = now;
            have_last = 1;
            continue;
        }

        double delta_s = (now.tv_sec - last_pulse_time.tv_sec) +
                          (now.tv_nsec - last_pulse_time.tv_nsec) / 1e9;
        double delta_us = delta_s * 1e6;

        if (delta_us < TACH_DEBOUNCE_US) {
            // Bounce/noise, not a real pulse. Don't update
            // last_pulse_time, so the next real pulse is measured
            // against the last good one.
            continue;
        }

        double inst_rpm = (1.0 / delta_s) / TACH_PULSES_PER_REV * 60.0;

        pthread_mutex_lock(&tach_mutex);
        tach_periods[tach_period_idx] = delta_s;
        tach_period_idx = (tach_period_idx + 1) % TACH_AVG_WINDOW;
        if (tach_period_count < TACH_AVG_WINDOW) {
            tach_period_count++;
        }

        double sum = 0.0;
        for (int i = 0; i < tach_period_count; i++) {
            sum += tach_periods[i];
        }
        double avg_period = sum / tach_period_count;
        double avg_rpm = (1.0 / avg_period) / TACH_PULSES_PER_REV * 60.0;
        tach_current_rpm = avg_rpm;
        pthread_mutex_unlock(&tach_mutex);

        printf("Tachometer: RPM = %.1f (inst %.1f)\n", avg_rpm, inst_rpm);

        last_pulse_time = now;
    }

    return NULL;
}

// Opens the GPIO chip/line and starts the tach worker thread. Call
// from operation_init(), after the other peripherals are set up.
int tach_init() {
    char chip_name[32];
    snprintf(chip_name, sizeof(chip_name), "gpiochip%d", TACH_GPIOCHIP);

    tach_chip = gpiod_chip_open_by_name(chip_name);
    if (!tach_chip) {
        fprintf(stderr, "Tachometer: failed to open %s. Run 'gpiodetect' "
                         "to list available chips.\n", chip_name);
        return -1;
    }

    tach_line = gpiod_chip_get_line(tach_chip, TACH_LINE);
    if (!tach_line) {
        fprintf(stderr, "Tachometer: failed to get line %d on %s\n",
                TACH_LINE, chip_name);
        gpiod_chip_close(tach_chip);
        tach_chip = NULL;
        return -1;
    }

    struct gpiod_line_request_config config = {0};
    config.consumer = "dime_time_tach";
    config.request_type = GPIOD_LINE_REQUEST_EVENT_FALLING_EDGE;

    if (gpiod_line_request(tach_line, &config, 0) < 0) {
        fprintf(stderr, "Tachometer: gpiod_line_request failed (try sudo, "
                         "or add yourself to the gpio group)\n");
        gpiod_chip_close(tach_chip);
        tach_chip = NULL;
        tach_line = NULL;
        return -1;
    }

    if (pthread_create(&tach_thread, NULL, tach_worker, NULL) != 0) {
        fprintf(stderr, "Tachometer: failed to create thread\n");
        gpiod_line_release(tach_line);
        gpiod_chip_close(tach_chip);
        tach_chip = NULL;
        tach_line = NULL;
        return -1;
    }

    tach_running = 1;
    printf("Tachometer initialized on gpiochip%d line %d "
           "(%d pulse(s)/rev, %d-sample average)\n",
           TACH_GPIOCHIP, TACH_LINE, TACH_PULSES_PER_REV, TACH_AVG_WINDOW);
    return 0;
}

// Joins the tach thread and releases the GPIO line/chip. Relies on
// the global `shutdown` flag being set to 1 already, same as the
// tilt/yaw threads. Safe to call even if tach_init() was never
// called or failed.
void tach_cleanup() {
    if (!tach_running) {
        return;
    }

    pthread_join(tach_thread, NULL);

    if (tach_line) {
        gpiod_line_release(tach_line);
        tach_line = NULL;
    }
    if (tach_chip) {
        gpiod_chip_close(tach_chip);
        tach_chip = NULL;
    }

    tach_running = 0;
}

// Thread-safe read of the current smoothed RPM, for anything else in
// the codebase (e.g. closed-loop speed control) that wants the value
// without parsing terminal output.
float get_tach_rpm() {
    pthread_mutex_lock(&tach_mutex);
    float rpm = (float)tach_current_rpm;
    pthread_mutex_unlock(&tach_mutex);
    return rpm;
}
// === END TACHOMETER ADDITION ===

void operation_init() {

    shutdown = 0;

    curr_tilt_angle = INITIAL_TILT_ANGLE;
    curr_yaw_angle = 0.0;

    //init mpu6050
    if (mpu6050_init(NULL) != 0) {
        fprintf(stderr, "Failed to initialize MPU6050. Is I2C enabled and the sensor connected?\n");
        return;
    }

    //init bts7960
    if (bts_init() != 0) {
        fprintf(stderr, "Failed to initialize BTS/BTN7960 HAL. Are you running as root?\n");
        return;
    }

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

    //init threads
    if (pthread_create(&tilt_thread, NULL, tilt_worker, NULL) != 0) {
        fprintf(stderr, "Failed to create tilt thread\n");
        return;
    }
    if (pthread_create(&yaw_thread, NULL, yaw_worker, NULL) != 0) {
        fprintf(stderr, "Failed to create yaw thread\n");
        return;
    }

    // === TACHOMETER ADDITION ===
    if (tach_init() != 0) {
        fprintf(stderr, "Failed to initialize tachometer\n");
        return;
    }
    // === END TACHOMETER ADDITION ===
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

    // === TACHOMETER ADDITION ===
    // shutdown is already 1 above; tach_worker polls it on a 2s
    // timeout, so this join returns within ~TACH_STALE_TIMEOUT_SEC.
    tach_cleanup();
    // === END TACHOMETER ADDITION ===

    tb6600_enable(&motor, 0);
    tb6600_close(&motor);

    mpu6050_close();

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

void tilt_with_feedback(float angle)
{
    mpu6050_data_t imu_data;

    const double target_angle = (double)angle;

    time_t start_time = time(NULL);

    while (1) {
        if (mpu6050_read(&imu_data) != 0) {
            fprintf(stderr, "Failed to read MPU6050\n");
            printf("STOP TILT ACTUATOR\n");
            bts_stop();
            return;
        }

        double current_angle = imu_data.stable_roll_deg;
        double error = target_angle - current_angle;

        fprintf(stderr,
                "Target: %.2f deg, Current: %.2f deg, Error: %.2f deg\n",
                target_angle,
                current_angle,
                error);

        if (fabs(error) <= TILT_TOLERANCE_DEG) {
            printf("Target tilt angle reached within tolerance. Stopping tilt actuator.\n");
            bts_stop();
            break;
        }

        if (error > 0.0) {
            printf("Tilting forward...\n");
            bts_forward_start(50);
        } else {
            printf("Tilting reverse...\n");
            bts_reverse_start(50);
        }

        if ((time(NULL) - start_time) >= TILT_TIMEOUT_SEC) {
            fprintf(stderr, "Tilt feedback timeout\n");
            printf("STOP TILT ACTUATOR\n");
            bts_stop();
            break;
        }

        usleep(TILT_LOOP_DELAY_US);
    }

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
    if (speed > 9999.0) {
        fprintf(stderr, "Invalid RPM: %.2f (must be 9999 or less). Skipping speed.\n", speed);
        return;
    }
    uint16_t mv = 0;
    //convert speed to mv
    mv = rpm_to_mv(speed);
    //(void)speed;
    printf("setting speed to %.2f mV\n", (float)mv);
    mcp4725_set_mv(&dac1, mv);
}

void speed_mv(float speed) {
    if (speed > 9999.0) {
        fprintf(stderr, "Invalid RPM: %.2f (must be 9999 or less). Skipping speed.\n", speed);
        return;
    }
    
    mcp4725_set_mv(&dac1, speed);
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
    //mcp4725_set_mv(&dac1, 1725);

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
