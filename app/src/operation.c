#include "app/src/include/operation.h"
#include <math.h>
#include <pthread.h>

float curr_tilt_angle = 0.0;
float curr_yaw_angle = 0.0;
float curr_speed = 0;
int curr_rpm = 0;

float d_angle = 0;

volatile int operation_initialized = 0;
volatile int hopper_enabled = 1; // 0 = enabled, 1 = disabled
volatile int hopper_running = 0;
volatile int launcher_running = 0;

#define TILT_TOLERANCE_DEG 0.2
#define TILT_TIMEOUT_SEC 15
#define TILT_LOOP_DELAY_US 50000
#define TILT_SETTLE_DELAY_US 120000
#define TILT_SAMPLE_COUNT 3
#define TILT_SAMPLE_DELAY_US 15000
#define TILT_TOLERANCE_HOLD_COUNT 2
#define TILT_FINE_WINDOW_DEG 1.5
#define TILT_MEDIUM_WINDOW_DEG 5.0
#define TILT_COARSE_WINDOW_DEG 15.0

#define HOPPER_PULSE_STEPS 11900
#define HOPPER_PULSE_START_DELAY_US 700
#define HOPPER_PULSE_END_DELAY_US 450
#define HOPPER_PULSE_ACCEL_STEPS 300
#define HOPPER_CONTINUOUS_DELAY_US 500

volatile float tilt_angle_w = 0;

tb6600_t motor;
static mcp4725_t dac1 = MCP4725_INIT_ZERO;
static pthread_t hopper_thread;
static volatile int hopper_thread_created = 0;

static void* hopper_step_thread(void *arg) {
    (void)arg;
    tb6600_step_continuous(&motor, HOPPER_CONTINUOUS_DELAY_US, &hopper_running);
    return NULL;
}

static uint16_t rpm_to_mv(float rpm) {
    // Linear mapping: 0 rpm -> 1380 mV, 1300 rpm -> 2200 mV
    double value = (820.0 / 1300.0) * rpm + 1380.0;

    return (uint16_t)value;
}

static double calibrate_tilt_angle(double raw_angle) {
    return (0.0008655 * raw_angle * raw_angle) + (0.9043201 * raw_angle) + 0.4493167;
}

static int read_tilt_feedback_angle(double *angle_out) {
    mpu6050_data_t imu_data;
    double angle_sum = 0.0;

    if (angle_out == NULL) {
        return -1;
    }

    for (int i = 0; i < TILT_SAMPLE_COUNT; i++) {
        if (mpu6050_read(&imu_data) != 0) {
            return -1;
        }

        angle_sum += (imu_data.roll_deg * 0.75) + (imu_data.stable_roll_deg * 0.25);

        if (i + 1 < TILT_SAMPLE_COUNT) {
            usleep(TILT_SAMPLE_DELAY_US);
        }
    }

    *angle_out = calibrate_tilt_angle(angle_sum / TILT_SAMPLE_COUNT);
    return 0;
}

static int get_tilt_duty_cycle(double error_deg) {
    double abs_error = fabs(error_deg);

    if (abs_error > TILT_COARSE_WINDOW_DEG) {
        return 100;
    }
    else if (abs_error > TILT_MEDIUM_WINDOW_DEG) {
        return 70;
    }
    else if (abs_error > TILT_FINE_WINDOW_DEG) {
        return 30;
    }
    else {
        return 20;
    }
}

static long tilt_angle_to_time(float i_angle, float f_angle) {
    float d_angle;
    long c_duration = 0;
    if (i_angle == f_angle) {
        return 0;
    }

    if (f_angle > i_angle) {
        if (i_angle <= 15.0) {
            if (f_angle <= 15.0) {
                d_angle = f_angle - i_angle;
                c_duration += d_angle * FTC_9_15;
                return c_duration;
            }
            c_duration += (15.0 - i_angle) * FTC_9_15;
            i_angle = 15.0;
        }

        if (i_angle <= 30.0) {
            if (f_angle <= 30.0) {
                d_angle = f_angle - i_angle;
                c_duration += d_angle * FTC_15_30;
                return c_duration;
            }
            c_duration += (30.0 - i_angle) * FTC_15_30;
            i_angle = 30.0;
        }

        if (i_angle <= 45.0) {
            if (f_angle <= 45.0) {
                d_angle = f_angle - i_angle;
                c_duration += d_angle * FTC_30_45;
                return c_duration;
            }
            c_duration += (45.0 - i_angle) * FTC_30_45;
            i_angle = 45.0;
        }

        if (i_angle <= 60.0) {
            if (f_angle <= 60.0) {
                d_angle = f_angle - i_angle;
                c_duration += d_angle * FTC_45_60;
                return c_duration;
            }
            c_duration += (60.0 - i_angle) * FTC_45_60;
            i_angle = 60.0;
        }

        if (i_angle <= 75.0) {
            if (f_angle <= 75.0) {
                d_angle = f_angle - i_angle;
                c_duration += d_angle * FTC_60_75;
                return c_duration;
            }
            c_duration += (75.0 - i_angle) * FTC_60_75;
            i_angle = 75.0;
        }

        if (i_angle <= 85.0) {
            d_angle = f_angle - i_angle;
            c_duration += d_angle * FTC_75_85;
            return c_duration;
        }
    }
    else {
        if (i_angle >= 75.0) {
            if (f_angle >= 75.0) {
                d_angle = i_angle - f_angle;
                c_duration += d_angle * RTC_75_85;
                return c_duration;
            }
            c_duration += (i_angle - 75.0) * RTC_75_85;
            i_angle = 75.0;
        }

        if (i_angle >= 60.0) {
            if (f_angle >= 60.0) {
                d_angle = i_angle - f_angle;
                c_duration += d_angle * RTC_60_75;
                return c_duration;
            }
            c_duration += (i_angle - 60.0) * RTC_60_75;
            i_angle = 60.0;
        }

        if (i_angle >= 45.0) {
            if (f_angle >= 45.0) {
                d_angle = i_angle - f_angle;
                c_duration += d_angle * RTC_45_60;
                return c_duration;
            }
            c_duration += (i_angle - 45.0) * RTC_45_60;
            i_angle = 45.0;
        }

        if (i_angle >= 30.0) {
            if (f_angle >= 30.0) {
                d_angle = i_angle - f_angle;
                c_duration += d_angle * RTC_30_45;
                return c_duration;
            }
            c_duration += (i_angle - 30.0) * RTC_30_45;
            i_angle = 30.0;
        }

        if (i_angle >= 15.0) {
            if (f_angle >= 15.0) {
                d_angle = i_angle - f_angle;
                c_duration += d_angle * RTC_15_30;
                return c_duration;
            }
            c_duration += (i_angle - 15.0) * RTC_15_30;
            i_angle = 15.0;
        }

        if (i_angle >= 9.0) {
            d_angle = i_angle - f_angle;
            c_duration += d_angle * RTC_9_15;
            return c_duration;
        }
    }

    return c_duration;
}

void operation_init() {

    fprintf(stderr, "[operation] operation_init() entered\n");

    if (operation_initialized) {
        return;
    }
    hopper_enabled = 1;
    hopper_running = 0;
    launcher_running = 0;

    curr_tilt_angle = INITIAL_TILT_ANGLE;
    curr_yaw_angle = 0.0;
    curr_tilt_angle = 0.0;
    curr_yaw_angle = 0.0;
    curr_speed = 0;
    curr_rpm = 0;

    /*fprintf(stderr, "[operation] initializing tachometer\n");
    if (tach_init() != 0) {
        fprintf(stderr, "Failed to initialize tachometer\n");
        return;
    }*/

    fprintf(stderr, "[operation] initializing MPU6050 IMU\n");
    if (mpu6050_init(NULL) != 0) {
        fprintf(stderr, "Failed to initialize MPU6050 — is I2C enabled?\n");
        return;
    }

    fprintf(stderr, "[operation] initializing BTS/BTN7960 motor driver\n");
    if (bts_init() != 0) {
        fprintf(stderr, "Failed to initialize BTS/BTN7960 HAL. Are you running as root?\n");
        return;
    }

    fprintf(stderr, "[operation] initializing TB6600 motor driver\n");
    if (tb6600_init(&motor, 1) < 0) {
        fprintf(stderr, "Failed to initialize TB6600\n");
        return;
    }
    tb6600_set_direction(&motor, 0);
    tb6600_enable(&motor, 1);

    fprintf(stderr, "[operation] initializing MCP4725 DAC\n");
    if (mcp4725_init(&dac1, MCP4725_I2C_BUS1, MCP4725_I2C_ADDR) != 0) {
        fprintf(stderr, "Failed to initialize MCP4725 — is I2C enabled?\n");
        return;
    }

    operation_initialized = 1;
    
    //homing_sequence();
}

void operation_cleanup() {

    //homing_sequence();

    if (!operation_initialized) {
        return;
    }

    hopper_stop();

    //tach_cleanup();
    mpu6050_close();
    mcp4725_set_raw(&dac1, 0);
    tb6600_enable(&motor, 1);
    tb6600_close(&motor);
    mcp4725_cleanup(&dac1);
    bts_cleanup();

    operation_initialized = 0;
}

void homing_sequence() {
    printf("Homing sequence initiated. Moving to default position...\n");
    //tilt_signal(INITIAL_TILT_ANGLE);
    //yaw_signal(0.0);
    tilt_with_feedback(INITIAL_TILT_ANGLE);
    curr_tilt_angle = INITIAL_TILT_ANGLE;
    curr_yaw_angle = 0.0;
}

void tilt_signal(float angle) {

    if (angle > 90.0 || angle < INITIAL_TILT_ANGLE) {
        fprintf(stderr, "Invalid tilt angle: %.2f degrees (must be between %.2f and 90 degrees). Skipping tilt.\n", angle, INITIAL_TILT_ANGLE);
        return;
    }

    //set rpm to 0
    mcp4725_set_raw(&dac1, 0);

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

    //set rpm to 0
    mcp4725_set_raw(&dac1, 0);

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
    curr_speed = (float)mv;
    curr_rpm = speed;
}

void set_speed(float speed) {
    if (speed > 1200.0) {
        fprintf(stderr, "Invalid RPM: %.2f (must be 1200 or less). Skipping speed.\n", speed);
        return;
    }
    uint16_t mv = 0;
    //convert speed to mv
    mv = rpm_to_mv(speed);

    curr_speed = (float)mv;
    curr_rpm = speed;
}

void percentage_to_mv(float percentage) {
    if (percentage > 100.0) {
        fprintf(stderr, "Invalid percentage: %.2f (must be 100 or less). Skipping speed.\n", percentage);
        return;
    }
    float mv = 1350.0 + (percentage / 100.0) * (4095.0 - 1350.0);
    printf("setting speed to %.2f mV\n", mv);
    
    if (launcher_running) {
        mcp4725_set_mv(&dac1, (uint16_t)mv);
    }
    curr_speed = mv;
    curr_rpm = 0; // Since we don't know the RPM corresponding to this raw value
}

void set_machine(int machine_position, int set_index) {
    mcp4725_set_raw(&dac1, 0);

    printf("Setting machine for set %d\n", set_index);
    printf("Tilt angle: %f, Yaw angle: %f, Speed: %f\n",
            set_seq[set_index]->tilt_angle,
            set_seq[set_index]->yaw_angle,
            set_seq[set_index]->rpm_output);
    
    //tilt_signal(set_seq[set_index]->tilt_angle);
    //yaw_signal(set_seq[set_index]->yaw_angle);
    //speed_signal(set_seq[set_index]->rpm_output);
}

void tilt_signal_advanced(float angle) {
    mcp4725_set_raw(&dac1, 0);
    tilt_signal(angle);
    speed_signal(curr_rpm);
}

void yaw_signal_advanced(float angle) {
    mcp4725_set_raw(&dac1, 0);
    yaw_signal(angle);
    speed_signal(curr_rpm);
}

void tilt_with_feedback(float angle) {

    if (angle > 90.0 || angle < INITIAL_TILT_ANGLE) {
        fprintf(stderr, "Invalid tilt angle: %.2f degrees (must be between %.2f and 90 degrees). Skipping tilt.\n", angle, INITIAL_TILT_ANGLE);
        return;
    }

    //first set speed to 0
    mcp4725_set_raw(&dac1, 0);

    const double target_angle = (double)angle;
    double current_angle = 0.0;
    int settled_reads = 0;

    time_t start_time = time(NULL);

    while (1) {
        if (read_tilt_feedback_angle(&current_angle) != 0) {
            fprintf(stderr, "Failed to read from MPU6050\n");
            bts_stop();
            return;
        }

        double error = target_angle - current_angle;

        fprintf(stderr, "Current angle: %.2f, Target angle: %.2f, Error: %.2f\n", current_angle, target_angle, error);

        if (fabs(error) <= TILT_TOLERANCE_DEG) {
            settled_reads++;

            if (settled_reads >= TILT_TOLERANCE_HOLD_COUNT) {
                fprintf(stderr, "Target angle reached within tolerance.\n");
                bts_stop();
                break;
            }

            usleep(TILT_SETTLE_DELAY_US);
            continue;
        }

        settled_reads = 0;

        int duty_cycle = get_tilt_duty_cycle(error);

        if (error > 0) {
            printf("Tilting forward at %d%%...\n", duty_cycle);
            bts_forward_start(duty_cycle);
        } else {
            printf("Tilting backward at %d%%...\n", duty_cycle);
            bts_reverse_start(duty_cycle);
        }

        usleep(TILT_LOOP_DELAY_US);

        if (difftime(time(NULL), start_time) >= TILT_TIMEOUT_SEC) {
            fprintf(stderr, "Tilt operation timed out after %d seconds.\n", TILT_TIMEOUT_SEC);
            bts_stop();
            break;
        }
    }

    curr_tilt_angle = (float)current_angle;

    //resume the speed after tilt operation if the machine was running
    if (launcher_running) {
        mcp4725_set_mv(&dac1, (uint16_t)curr_speed);
    }
}

void toggle_hopper() {
    if (hopper_running) {
        hopper_stop();
    } else {
        hopper_start();
    }
}

void hopper_start() {
    //set rpm to 0
    //mcp4725_set_raw(&dac1, 0);

    if (!motor.request) {
        fprintf(stderr, "Cannot start hopper: motor not initialized\n");
        return;
    }

    if (hopper_running) {
        printf("Hopper already running\n");
        return;
    }

    hopper_running = 1;

    tb6600_enable(&motor, 1);

    if (pthread_create(&hopper_thread, NULL, hopper_step_thread, NULL) != 0) {
        perror("Failed to start hopper thread");
        hopper_running = 0;
        tb6600_enable(&motor, 0);
        return;
    }

    hopper_thread_created = 1;
    
    hopper_enabled = 0;
    printf("Hopper started\n");

    //speed_signal(curr_rpm);
}

void hopper_stop() {
    //set rpm to 0
    //mcp4725_set_raw(&dac1, 0);

    hopper_running = 0;

    if (hopper_thread_created) {
        pthread_join(hopper_thread, NULL);
        hopper_thread_created = 0;
    }

    if (motor.request) {
        tb6600_enable(&motor, 0);
    }

    hopper_enabled = 1;
    printf("Hopper stopped\n");

    //speed_signal(curr_rpm);
}

void hopper_pulse() {
    //set rpm to 0
    //mcp4725_set_raw(&dac1, 0);

    if (!motor.request) {
        fprintf(stderr, "Cannot pulse hopper: motor not initialized\n");
        return;
    }

    //turn hopper off if it is running
    if (hopper_running) {
        hopper_stop();
    }

    printf("Pulsing hopper...\n");
    tb6600_enable(&motor, 1);
    tb6600_step_accel(&motor, HOPPER_PULSE_STEPS, HOPPER_PULSE_START_DELAY_US, HOPPER_PULSE_END_DELAY_US, HOPPER_PULSE_ACCEL_STEPS);
    tb6600_enable(&motor, 0);
    
    printf("Hopper pulse complete.\n");

    //speed_signal(curr_rpm);
}

float get_tilt_angle() {
    return curr_tilt_angle;
}

float get_yaw_angle() {
    return curr_yaw_angle;
}

int get_speed() {
    return curr_speed;
}

int get_rpm() {
    return curr_rpm;
}

void pause_machine() {
    //pause the machine
    //printf("Pausing machine...\n");
    mcp4725_set_raw(&dac1, 0);

    launcher_running = 0;

    return;
}

void resume_machine() {
    //signal speed
    //printf("Resuming machine at %.2f mv\n", (float)curr_speed);
    mcp4725_set_mv(&dac1, (uint16_t)curr_speed);

    launcher_running = 1;

    return;
}

int get_tach_reading() {
    //int rpm = (int)get_tach_rpm();
    // printf("Current RPM: %d\n", rpm);
    //return rpm;
    return 1;
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