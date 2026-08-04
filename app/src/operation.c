#include "app/src/include/operation.h"
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <gpiod.h>

#include "log.h"

float curr_tilt_angle = 0.0;
float curr_speed = 0;
int curr_rpm = 0;

float d_angle = 0;

volatile int operation_initialized = 0;
volatile int hopper_enabled = 1; // 0 = enabled, 1 = disabled
volatile int hopper_running = 0;
static volatile int hopper_pulse_running = 0;
static volatile sig_atomic_t homing_running = 0;
static volatile sig_atomic_t homing_cancel_requested = 0;
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
#define TILT_STALL_EPSILON_DEG 0.05
#define TILT_STALL_ADJUSTMENTS 30
#define SPEED_STALL_EPSILON_RPM 5
#define SPEED_STALL_ADJUSTMENTS 8
#define SPEED_TOLERANCE_RPM 25
#define SPEED_TOLERANCE_HOLD_COUNT 2
#define SPEED_FEEDBACK_TIMEOUT_SEC 30
#define SPEED_CONTROL_DELAY_US 250000
#define SPEED_STABLE_DELTA_RPM 20
#define SPEED_STABLE_READ_COUNT 2
#define SPEED_PROPORTIONAL_GAIN 0.05
#define SPEED_MIN_ADJUSTMENT_MV 5
#define SPEED_MAX_ADJUSTMENT_MV 25
#define SPEED_CACHE_RPM_EPSILON 0.5
#define MAX_LAUNCH_VOLTAGE_MV MCP4725_THROTTLE_MAX_MV
#define RPM_MAP_BASE_MV 1214.83
#define RPM_MAP_MV_PER_RPM 0.55558

#define HOPPER_PULSE_STEPS 2400
#define HOPPER_PULSE_START_DELAY_US 1000
#define HOPPER_PULSE_END_DELAY_US 500
#define HOPPER_PULSE_ACCEL_STEPS 400
#define HOPPER_PULSE_MAX_ATTEMPTS 4 // How many tries when no ball is detected after a pulse
#define HOPPER_CONTINUOUS_DELAY_US 500
#define HOPPER_RESET_INTERVAL_PULSES 4
#define HOPPER_SENSOR_RUN_ON_US 1000000
#define HOPPER_SENSOR_RUN_ON_SLICE_US 10000
#define HOPPER_RESET_MIN_RUN_US 250000
#define HOMING_FLYWHEEL_RPM 300.0f
#define HOPPER_RESET_TIMEOUT_SEC 30
#define HOPPER_RESET_POLL_DELAY_US 10000
#define HOPPER_PRELAUNCH_DELAY_US 5000000 // settle time before each push so players can get into position
#define HOPPER_PRELAUNCH_SLICE_US 50000   // poll interval so an interrupt aborts the wait promptly

volatile float tilt_angle_w = 0;

tb6600_t motor;
static mcp4725_t dac1 = MCP4725_INIT_ZERO;
static pthread_t hopper_thread;
static volatile int hopper_thread_created = 0;
static unsigned int hopper_pulse_count = 0;
static volatile int hopper_alignment_required = 1;
static volatile unsigned int component_status_mask = 0;
static volatile int feedback_sensor_fault = 0;
static int speed_cache_valid = 0;
static float speed_cache_rpm = 0.0f;
static float speed_cache_mv = 0.0f;

void operation_clear_feedback_fault(void) {
    feedback_sensor_fault = 0;
}

const char *operation_feedback_fault_message(void) {
    if (feedback_sensor_fault == 1) {
        return "Tilt sensor is not responding. Use Manual mode and check that the tilt sensor is connected.";
    }
    if (feedback_sensor_fault == 2) {
        return "Speed sensor is not responding. Use Manual mode and check that the speed sensor is connected.";
    }
    if (feedback_sensor_fault == 3) {
        return "Speed could not stabilize automatically. Use Manual mode to set the speed.";
    }
    return NULL;
}

#define LAUNCH_BEEP_COUNT 3
#define LAUNCH_BEEP_FREQUENCY_HZ 880
// 3 beeps. First 2 shorter, last longer.
#define LAUNCH_BEEP_SHORT_DURATION_MS 300
#define LAUNCH_BEEP_LONG_DURATION_MS (LAUNCH_BEEP_SHORT_DURATION_MS * 3)
#define LAUNCH_BEEP_GAP_MS 400
#define LAUNCH_BEEP_SAMPLE_RATE 48000
#define LAUNCH_BEEP_AMPLITUDE 30000.0
#define LAUNCH_BEEP_PREROLL_MS 1000

static int play_launch_warning(void) {
    for (int i = 0; i < LAUNCH_BEEP_COUNT; i++) {
        int is_last = (i == LAUNCH_BEEP_COUNT - 1);
        buzzer_tone(is_last ? LAUNCH_BEEP_LONG_DURATION_MS : LAUNCH_BEEP_SHORT_DURATION_MS);
        if (!is_last) {
            usleep(LAUNCH_BEEP_GAP_MS * 1000);
        }
    }
    return 0;
}

/*
static void *play_launch_warning_thread(void *arg) {
    (void)arg;
    play_launch_warning();
    return NULL;
}
    */

/*
 * Software interrupt (emergency abort) support.
 *
 * g_operation_interrupt is a sig_atomic_t so it can be set safely from a
 * signal handler. It is checked cooperatively by the blocking loops in
 * this file (tilt_with_feedback, speed_with_feedback, ...) so they can
 * bail out and leave the motors stopped instead of running to completion.
 *
 * hopper_running / launcher_running are also cleared directly from the
 * handler because they are the exit conditions already polled by
 * tb6600_step_continuous() and speed_with_feedback()'s loop -- clearing
 * them is what actually breaks out of those blocking HAL calls, rather
 * than waiting for the next cooperative check.
 */
static volatile sig_atomic_t g_operation_interrupt = 0;

static void operation_interrupt_handler(int signo) {
    (void)signo;
    g_operation_interrupt = 1;
    hopper_running = 0;
    hopper_pulse_running = 0;
    if (homing_running) homing_cancel_requested = 1;
    launcher_running = 0;
}

void operation_install_interrupt_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = operation_interrupt_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // deliberately no SA_RESTART: let blocking syscalls (usleep, etc.) return early

    if (sigaction(SIGUSR1, &sa, NULL) != 0) {
        perror("Failed to install SIGUSR1 interrupt handler");
    }
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        perror("Failed to install SIGINT interrupt handler");
    }
}

void operation_request_interrupt(void) {
    operation_interrupt_handler(0);
}

void operation_force_stop(void) {
    operation_interrupt_handler(0);
    hopper_alignment_required = 1;

    if (!operation_initialized) {
        return;
    }

    // Stop each driver explicitly as well as setting the cooperative flags.
    // This immediately removes power from the actuator, flywheel, and hopper.
    bts_stop();
    mcp4725_set_raw(&dac1, 0);
    hopper_stop();
    curr_speed = 0.0f;
    curr_rpm = 0;
    speed_cache_valid = 0;
    fprintf(stderr, "[operation] Emergency stop: all motion stopped.\n");
}

int operation_interrupt_pending(void) {
    return g_operation_interrupt;
}

void operation_clear_interrupt(void) {
    g_operation_interrupt = 0;
}

static void* hopper_step_thread(void *arg) {
    (void)arg;
    tb6600_step_continuous(&motor, HOPPER_CONTINUOUS_DELAY_US, &hopper_running);
    return NULL;
}

static float clamp_rpm_to_voltage_limit(float rpm) {
    const float max_rpm = (float)((MAX_LAUNCH_VOLTAGE_MV - RPM_MAP_BASE_MV) /
                                  RPM_MAP_MV_PER_RPM);
    if (rpm < 0.0f) return 0.0f;
    if (rpm > max_rpm) {
        fprintf(stderr,
                "Requested %.2f RPM exceeds the %.1f RPM represented by the 4.2 V limit; clamping.\n",
                rpm, max_rpm);
        return max_rpm;
    }
    return rpm;
}

static uint16_t rpm_to_mv(float rpm) {
    rpm = clamp_rpm_to_voltage_limit(rpm);
    // Calibrated feed-forward mapping: V(r) = 0.55558r + 1214.83 mV.
    double value = RPM_MAP_MV_PER_RPM * rpm + RPM_MAP_BASE_MV;

    if (value > MAX_LAUNCH_VOLTAGE_MV) value = MAX_LAUNCH_VOLTAGE_MV;

    return (uint16_t)value;
}

static double calibrate_tilt_angle(double raw_angle) {
    return (0.0001033733f * raw_angle * raw_angle) + (1.05823572f * raw_angle) + 0.38731707f;
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

        if (i_angle <= 87.0) {
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

    operation_install_interrupt_handler();

    hopper_enabled = 1;
    hopper_running = 0;
    launcher_running = 0;

    //curr_tilt_angle = INITIAL_TILT_ANGLE;
    curr_speed = 0;
    curr_rpm = 0;
    speed_cache_valid = 0;
    component_status_mask = 0;

    fprintf(stderr, "[operation] initializing tachometer\n");
    if (tach_init() != 0) {
        fprintf(stderr, "Failed to initialize tachometer\n");
        return;
    }
    component_status_mask |= COMPONENT_TACHOMETER;

    fprintf(stderr, "[operation] initializing MPU6050 IMU\n");
    if (mpu6050_init(NULL) != 0) {
        fprintf(stderr, "Failed to initialize MPU6050 — is I2C enabled?\n");
        return;
    }
    component_status_mask |= COMPONENT_IMU;
    double initial_tilt_angle = 0.0;
    if (read_tilt_feedback_angle(&initial_tilt_angle) == 0) {
        curr_tilt_angle = (float)initial_tilt_angle;
    }

    fprintf(stderr, "[operation] initializing BTS/BTN7960 motor driver\n");
    if (bts_init() != 0) {
        fprintf(stderr, "Failed to initialize BTS/BTN7960 HAL. Are you running as root?\n");
        return;
    }
    component_status_mask |= COMPONENT_TILT_DRIVER;

    fprintf(stderr, "[operation] initializing TB6600 motor driver\n");
    if (tb6600_init(&motor, 1) < 0) {
        fprintf(stderr, "Failed to initialize TB6600\n");
        return;
    }
    component_status_mask |= COMPONENT_HOPPER_STEPPER;
    tb6600_set_direction(&motor, 0);
    tb6600_enable(&motor, 1);

    fprintf(stderr, "[operation] initializing MCP4725 DAC\n");
    if (mcp4725_init(&dac1, MCP4725_I2C_BUS1, MCP4725_I2C_ADDR) != 0) {
        fprintf(stderr, "Failed to initialize MCP4725 — is I2C enabled?\n");
        return;
    }

    if (mcp4725_write_eeprom(&dac1, 0) != 0) {
        fprintf(stderr, "Failed to write MCP4725 EEPROM power-on default\n");
        return;
    }
    component_status_mask |= COMPONENT_FLYWHEEL_DAC;

    fprintf(stderr, "[operation] initializing ball presence sensor (HC-SR04)\n");
    if (hcsr04_init() != 0) {
        fprintf(stderr, "Failed to initialize HC-SR04 ball sensor\n");
        return;
    }
    component_status_mask |= COMPONENT_BALL_SENSOR;

    operation_initialized = 1;
    
    //homing_sequence();
}

void operation_cleanup() {

    //homing_sequence();

    if (!operation_initialized) {
        return;
    }

    hopper_stop();

    hcsr04_cleanup();
    tach_cleanup();
    mpu6050_close();
    mcp4725_set_raw(&dac1, 0);
    tb6600_enable(&motor, 1);
    tb6600_close(&motor);
    mcp4725_cleanup(&dac1);
    bts_cleanup();
    buzzer_cleanup();

    operation_initialized = 0;
    speed_cache_valid = 0;
}

void homing_sequence() {
    homing_running = 1;
    homing_cancel_requested = 0;
    LOG_INFO("Homing sequence initiated. Moving to default position...\n");

    // Start from a known stopped state so tilt_signal() cannot restore a
    // previously configured launch voltage after moving the tilt mechanism.
    hopper_stop();
    mcp4725_set_raw(&dac1, 0);
    launcher_running = 0;
    curr_speed = 0.0f;
    curr_rpm = 0;
    speed_cache_valid = 0;

    // 1. Return the launcher to its mechanical starting angle while the
    // flywheel and hopper are stopped.
    tilt_signal(INITIAL_TILT_ANGLE);
    if (homing_cancel_requested) {
        fprintf(stderr, "Homing cancelled while returning tilt to home.\n");
        goto homing_stop;
    }
    if (feedback_sensor_fault) {
        fprintf(stderr, "Homing aborted: unable to reach the initial tilt position.\n");
        goto homing_stop;
    }

    // 2. Run the flywheel at a dedicated low speed only while finding the
    // hopper home sensor. This direct voltage estimate intentionally avoids
    // waiting for normal launch-speed feedback during homing.
    LOG_INFO("Homing: running flywheel at %.0f RPM.\n", HOMING_FLYWHEEL_RPM);
    speed_signal(HOMING_FLYWHEEL_RPM);
    launcher_running = 1;

    if (homing_cancel_requested) {
        fprintf(stderr, "Homing cancelled before hopper reset.\n");
        goto homing_stop;
    }

    // 3. Find and settle at the hopper home position.
    hopper_reset();
    hopper_pulse_count = 0;

homing_stop:
    // 4. Homing always leaves the tilt, hopper, and flywheel motors stopped,
    // including when tilt feedback or hopper reset aborts early.
    bts_stop();
    hopper_stop();
    mcp4725_set_raw(&dac1, 0);
    curr_speed = 0.0f;
    curr_rpm = 0;
    launcher_running = 0;
    speed_cache_valid = 0;
    homing_running = 0;
    LOG_INFO("Homing sequence finished with all motors stopped.\n");
}

void tilt_signal(float angle) {

    operation_clear_feedback_fault();

    if (angle > 87.0 || angle < INITIAL_TILT_ANGLE) {
        fprintf(stderr, "Invalid tilt angle: %.2f degrees (must be between %.2f and 87 degrees). Skipping tilt.\n", angle, INITIAL_TILT_ANGLE);
        return;
    }

    if (operation_interrupt_pending()) {
        // forward_ms()/reverse_ms() are fixed-duration blocking HAL calls
        // that can't be aborted mid-flight, so refuse to start a new tilt
        // move while an interrupt is pending rather than run to completion.
        fprintf(stderr, "Tilt signal aborted before start due to pending interrupt.\n");
        operation_clear_interrupt();
        return;
    }

    //set rpm to 0
    mcp4725_set_raw(&dac1, 0);

    float delta_angle = angle - curr_tilt_angle;
    long duration_us = tilt_angle_to_time(curr_tilt_angle, angle);

    if (delta_angle == 0) {
        LOG_DEBUG("No Change in tilt angle\n");
        if (launcher_running) {
            mcp4725_set_mv(&dac1, (uint16_t)curr_speed);
        }
        return;
    }

    if (curr_tilt_angle > 80.0) {
        if (angle > 80.0) {
            if (delta_angle > 0) {
                forward_ms(100, duration_us);
            } else if (delta_angle < 0) {
                reverse_ms(100, duration_us);
            }
        } else {
            tilt_with_feedback(angle);
            if (feedback_sensor_fault) return;
        }
    } else {
        if (angle < 80.0) {
            tilt_with_feedback(angle);
            if (feedback_sensor_fault) return;
        } else {
            tilt_with_feedback(80.0);
            if (feedback_sensor_fault) return;
            usleep(100000); // Small delay to ensure the tilt operation completes
            duration_us = tilt_angle_to_time(80.0, angle);
            forward_ms(100, duration_us);
        }
    }

    curr_tilt_angle = angle;

    //resume the speed after tilt operation if the machine was running
    if (launcher_running) {
        mcp4725_set_mv(&dac1, (uint16_t)curr_speed);
    }
}

void speed_signal(float speed) {
    if (speed < 0.0) {
        fprintf(stderr, "Invalid RPM: %.2f (must not be negative). Skipping speed.\n", speed);
        return;
    }
    speed = clamp_rpm_to_voltage_limit(speed);
    speed_cache_valid = 0;
    uint16_t mv = 0;
    //convert speed to mv
    mv = rpm_to_mv(speed);
    //(void)speed;
    LOG_DEBUG("setting speed to %.2f mV\n", (float)mv);
    mcp4725_set_mv(&dac1, mv);
    curr_speed = (float)mv;
    curr_rpm = speed;
}

void set_speed(float speed) {
    if (speed < 0.0) {
        fprintf(stderr, "Invalid RPM: %.2f (must not be negative). Skipping speed.\n", speed);
        return;
    }
    speed = clamp_rpm_to_voltage_limit(speed);
    uint16_t mv = 0;
    //convert speed to mv
    mv = rpm_to_mv(speed);

    curr_speed = (float)mv;
    curr_rpm = speed;
}

void percentage_to_mv(float percentage) {
    if (percentage < 0.0 || percentage > 100.0) {
        fprintf(stderr, "Invalid percentage: %.2f (must be between 0 and 100). Skipping speed.\n", percentage);
        return;
    }
    speed_cache_valid = 0;

    // Zero is an explicit motor-off command. Apply the minimum-running
    // voltage offset only to non-zero Manual speed requests.
    float mv = percentage == 0.0
        ? 0.0
        : 1350.0 + (percentage / 100.0) * (MAX_LAUNCH_VOLTAGE_MV - 1350.0);
    LOG_DEBUG("setting speed to %.2f mV\n", mv);
    
    if (launcher_running) {
        mcp4725_set_mv(&dac1, (uint16_t)mv);
    }
    curr_speed = mv;
    curr_rpm = 0; // Since we don't know the RPM corresponding to this raw value
}

void set_machine(int machine_position, int set_index) {
    if (machine_position < 0 || machine_position >= NUM_MACHINE_POSITIONS) {
        fprintf(stderr, "Invalid machine position: %d\n", machine_position);
        return;
    }

    if (set_index < 0 || set_index >= NUM_SETS) {
        fprintf(stderr, "Invalid set index: %d\n", set_index);
        return;
    }

    set_specs_t *spec = &set_seq[machine_position][set_index];

    LOG_INFO("Setting machine %d for set %d\n", machine_position, set_index);
    LOG_INFO("Tilt angle: %f, Speed (RPM): %f\n",
            spec->tilt_angle, spec->rpm_output);

    const float target_rpm = clamp_rpm_to_voltage_limit(spec->rpm_output);
    const int reuse_speed = speed_cache_valid &&
        fabsf(target_rpm - speed_cache_rpm) <= SPEED_CACHE_RPM_EPSILON;

    // Configure the launch speed target first so that once tilt_signal()
    // reaches the target angle it resumes the flywheel (if already running)
    // at the correct value for this set rather than the previous one.
    if (reuse_speed) {
        curr_speed = speed_cache_mv;
        curr_rpm = (int)target_rpm;
        LOG_DEBUG("Reusing %.2f mV for consecutive %.2f RPM launch.\n",
               speed_cache_mv, target_rpm);
    } else {
        speed_cache_valid = 0;
        set_speed(target_rpm);
    }

    // Blocking feedback-controlled tilt move to this set's angle.
    tilt_signal(spec->tilt_angle);
    if (feedback_sensor_fault) return;

    // The launch modes start the flywheel before applying a set. Refine the
    // configured speed using tachometer feedback and return only once settled.
    if (!reuse_speed) {
        speed_with_feedback(target_rpm);
    }
}

void tilt_with_feedback(float angle) {

    const double target_angle = (double)angle;
    double current_angle = 0.0;
    double previous_angle = NAN;
    int settled_reads = 0;
    int unchanged_adjustments = 0;

    time_t start_time = time(NULL);

    while (1) {
        if (operation_interrupt_pending()) {
            fprintf(stderr, "Tilt operation interrupted -- stopping motor.\n");
            bts_stop();
            operation_clear_interrupt();
            return;
        }

        if (read_tilt_feedback_angle(&current_angle) != 0) {
            fprintf(stderr, "Failed to read from MPU6050\n");
            feedback_sensor_fault = 1;
            bts_stop();
            return;
        }

        double error = target_angle - current_angle;

        LOG_DEBUG("Current angle: %.2f, Target angle: %.2f, Error: %.2f\n", current_angle, target_angle, error);

        if (fabs(error) <= TILT_TOLERANCE_DEG) {
            settled_reads++;

            if (settled_reads >= TILT_TOLERANCE_HOLD_COUNT) {
                LOG_INFO("Target angle reached within tolerance.\n");
                bts_stop();
                break;
            }

            usleep(TILT_SETTLE_DELAY_US);
            continue;
        }

        settled_reads = 0;

        if (!isnan(previous_angle) &&
            fabs(current_angle - previous_angle) <= TILT_STALL_EPSILON_DEG) {
            unchanged_adjustments++;
        } else {
            unchanged_adjustments = 0;
        }
        previous_angle = current_angle;

        if (unchanged_adjustments >= TILT_STALL_ADJUSTMENTS) {
            fprintf(stderr, "Tilt sensor did not change after %d motor adjustments; aborting.\n",
                    TILT_STALL_ADJUSTMENTS);
            feedback_sensor_fault = 1;
            bts_stop();
            return;
        }

        int duty_cycle = get_tilt_duty_cycle(error);

        if (error > 0) {
            LOG_DEBUG("Tilting forward at %d%%...\n", duty_cycle);
            bts_forward_start(duty_cycle);
        } else {
            LOG_DEBUG("Tilting backward at %d%%...\n", duty_cycle);
            bts_reverse_start(duty_cycle);
        }

        usleep(TILT_LOOP_DELAY_US);

        if (difftime(time(NULL), start_time) >= TILT_TIMEOUT_SEC) {
            fprintf(stderr, "Tilt operation timed out after %d seconds.\n", TILT_TIMEOUT_SEC);
            bts_stop();
            break;
        }
    }
}

void speed_with_feedback(float rpm) {
    operation_clear_feedback_fault();
    if (rpm < 0.0) {
        fprintf(stderr, "Invalid RPM: %.2f (must not be negative). Skipping speed.\n", rpm);
        return;
    }
    rpm = clamp_rpm_to_voltage_limit(rpm);
    int mv = 0;
    //convert speed to mv
    mv = rpm_to_mv(rpm);
    int previous_rpm = -1;
    int previous_adjustment_rpm = -1;
    int unchanged_adjustments = 0;
    int settled_reads = 0;
    int stable_reads = 0;
    int feedback_completed = 0;
    time_t start_time = time(NULL);

    while (launcher_running) {
        if (operation_interrupt_pending()) {
            fprintf(stderr, "Speed operation interrupted -- stopping motor.\n");
            mcp4725_set_raw(&dac1, 0);
            operation_clear_interrupt();
            break;
        }

        //tach reading
        int actual_rpm = get_tach_rpm();

        if (abs(actual_rpm - (int)rpm) <= SPEED_TOLERANCE_RPM) {
            settled_reads++;
            if (settled_reads >= SPEED_TOLERANCE_HOLD_COUNT) {
                LOG_INFO(
                        "Target speed reached within tolerance: actual=%d RPM, target=%.2f RPM, voltage=%d mV.\n",
                        actual_rpm, rpm, mv);
                feedback_completed = 1;
                break;
            }
        } else {
            settled_reads = 0;
        }

        if (previous_rpm >= 0 &&
            abs(actual_rpm - previous_rpm) <= SPEED_STABLE_DELTA_RPM) {
            stable_reads++;
        } else {
            stable_reads = 0;
        }
        previous_rpm = actual_rpm;

        // Let motor inertia settle before changing the DAC again. Once stable,
        // make a bounded proportional correction instead of a fixed 10 mV step.
        if (stable_reads >= SPEED_STABLE_READ_COUNT &&
            abs(actual_rpm - (int)rpm) > SPEED_TOLERANCE_RPM) {
            if (previous_adjustment_rpm >= 0 &&
                abs(actual_rpm - previous_adjustment_rpm) <= SPEED_STALL_EPSILON_RPM) {
                unchanged_adjustments++;
            } else {
                unchanged_adjustments = 0;
            }
            previous_adjustment_rpm = actual_rpm;

            if (unchanged_adjustments >= SPEED_STALL_ADJUSTMENTS) {
                fprintf(stderr, "Speed sensor did not change after %d DAC adjustments; aborting.\n",
                        SPEED_STALL_ADJUSTMENTS);
                feedback_sensor_fault = 2;
                mcp4725_set_raw(&dac1, 0);
                launcher_running = 0;
                break;
            }

            int adjustment = (int)lround(((double)rpm - actual_rpm) *
                                         SPEED_PROPORTIONAL_GAIN);
            if (adjustment > SPEED_MAX_ADJUSTMENT_MV) adjustment = SPEED_MAX_ADJUSTMENT_MV;
            if (adjustment < -SPEED_MAX_ADJUSTMENT_MV) adjustment = -SPEED_MAX_ADJUSTMENT_MV;
            if (adjustment > 0 && adjustment < SPEED_MIN_ADJUSTMENT_MV) adjustment = SPEED_MIN_ADJUSTMENT_MV;
            if (adjustment < 0 && adjustment > -SPEED_MIN_ADJUSTMENT_MV) adjustment = -SPEED_MIN_ADJUSTMENT_MV;

            mv += adjustment;
            if (mv > MAX_LAUNCH_VOLTAGE_MV) mv = MAX_LAUNCH_VOLTAGE_MV;
            if (mv < 0) mv = 0;
            fprintf(stderr,
                    "Speed feedback correction: actual=%d RPM, target=%.2f RPM, adjustment=%d mV, voltage=%d mV.\n",
                    actual_rpm, rpm, adjustment, mv);
            mcp4725_set_mv(&dac1, (uint16_t)mv);
            stable_reads = 0;
        }

        usleep(SPEED_CONTROL_DELAY_US);

        if (difftime(time(NULL), start_time) >= SPEED_FEEDBACK_TIMEOUT_SEC) {
            fprintf(stderr, "Speed feedback timed out after %d seconds.\n",
                    SPEED_FEEDBACK_TIMEOUT_SEC);
            mcp4725_set_raw(&dac1, 0);
            launcher_running = 0;
            feedback_sensor_fault = 3;
            break;
        }
    }
    if (feedback_sensor_fault == 2 || feedback_sensor_fault == 3) {
        curr_speed = 0.0f;
        curr_rpm = 0;
    } else {
        curr_speed = (float)mv;
        curr_rpm = rpm;
        if (feedback_completed) {
            speed_cache_valid = 1;
            speed_cache_rpm = rpm;
            speed_cache_mv = (float)mv;
            printf("Cached %.2f mV for %.2f RPM.\n", speed_cache_mv, speed_cache_rpm);
        }
    }
}

void toggle_hopper() {
    if (hopper_running) {
        hopper_stop();
    } else {
        hopper_start();
    }
}

// Actual hopper-start implementation, deliberately NOT gated on
// launcher_running: hopper_reset() (and therefore homing_sequence()) must be
// able to spin the hopper stepper to find its home sensor regardless of
// whether the launcher flywheel happens to be running. Only reachable via
// hopper_start() (gated, below) or hopper_reset()/homing_sequence().
static void hopper_start_internal(void) {
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

// Public, user/gesture-facing entry point: the hopper must never feed balls
// into a flywheel that isn't spinning, so refuse to start unless the launcher
// is running (see resume_machine()/pause_machine()). Returns 0 on success,
// -1 if refused (or if the underlying start attempt otherwise failed).
int hopper_start(void) {
    if (!launcher_running) {
        fprintf(stderr, "Cannot start hopper: machine is not running. Start the launcher before starting the hopper.\n");
        return -1;
    }

    hopper_start_internal();
    return hopper_running ? 0 : -1;
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

// Returns 0 on success, -1 if refused (motor not initialized, machine not
// running, or an interrupt was pending).
int hopper_pulse(void) {
    //set rpm to 0
    //mcp4725_set_raw(&dac1, 0);

    if (!launcher_running) {
        fprintf(stderr, "Cannot pulse hopper: machine is not running\n");
        return -1;
    }

    if (!motor.request) {
        fprintf(stderr, "Cannot pulse hopper: motor not initialized\n");
        return -1;
    }

    // The hopper must never feed balls into a flywheel that isn't spinning.
    if (!launcher_running) {
        fprintf(stderr, "Cannot pulse hopper: machine is not running. Start the launcher before pulsing the hopper.\n");
        return -1;
    }

    if (operation_interrupt_pending()) {
        fprintf(stderr, "Hopper pulse aborted before start due to pending interrupt.\n");
        operation_clear_interrupt();
        return -1;
    }

    //turn hopper off if it is running
    if (hopper_running) {
        hopper_stop();
    }

    // Sound the warning once, before the feeder moves to launch the first
    // ball. This blocks: the feeder does not move until the beeps finish.
    printf("Sounding %d warning beeps before feeding ball...\n", LAUNCH_BEEP_COUNT);
    play_launch_warning();

    int attempt;
    int fed_ball = 0;

    for (attempt = 1; attempt <= HOPPER_PULSE_MAX_ATTEMPTS; attempt++) {
        if (operation_interrupt_pending()) {
            fprintf(stderr, "Hopper pulse aborted before movement.\n");
            operation_clear_interrupt();
            return -1;
        }

        hopper_pulse_count++;
        if (hopper_pulse_count >= HOPPER_RESET_INTERVAL_PULSES) {
            hopper_pulse_count = 0;
            printf("Hopper pulse #%d: running reset instead of pulse.\n",
                   HOPPER_RESET_INTERVAL_PULSES);

            hopper_reset();

            float d = hcsr04_get_distance_cm();
            fprintf(stderr, "  distance: %.2f cm\n", d);
            if (hcsr04_ball_present_debounced()) {
                printf("Ball detected after reset (attempt %d/%d).\n", attempt, HOPPER_PULSE_MAX_ATTEMPTS);
                fed_ball = 1;
                break;
            }

            fprintf(stderr, "No ball detected after reset (attempt %d/%d).\n", attempt, HOPPER_PULSE_MAX_ATTEMPTS);
            continue;
        }

        printf("Pulsing hopper (attempt %d/%d)...\n",
               attempt, HOPPER_PULSE_MAX_ATTEMPTS);

        // Only start the settle countdown once a ball is actually seated, so an
        // empty/misfed hopper doesn't burn the delay before retrying.
        if (attempt == 1 || ll_present_debounced()) {
            uint32_t waited_us = 0;
            while (waited_us < HOPPER_PRELAUNCH_DELAY_US) {
                if (operation_interrupt_pending()) {
                    fprintf(stderr, "Hopper pulse aborted during pre-launch delay.\n");
                    operation_clear_interrupt();
                    tb6600_enable(&motor, 0);
                    return -1;
                }
                usleep(HOPPER_PRELAUNCH_SLICE_US);
                waited_us += HOPPER_PRELAUNCH_SLICE_US;
            }
        }

        tb6600_enable(&motor, 1);
        hopper_pulse_running = 1;
        tb6600_step_accel_interruptible(
            &motor, HOPPER_PULSE_STEPS, HOPPER_PULSE_START_DELAY_US,
            HOPPER_PULSE_END_DELAY_US, HOPPER_PULSE_ACCEL_STEPS,
            &hopper_pulse_running);
        hopper_pulse_running = 0;
        tb6600_enable(&motor, 0);

        //if (warning_thread_started) {
        //    pthread_join(warning_thread, NULL);
        //}

        if (operation_interrupt_pending()) {
            fprintf(stderr, "Hopper pulse interrupted during stepping.\n");
            operation_clear_interrupt();
            return -1;
        }

        printf("Hopper pulse complete.\n");

        float d = hcsr04_get_distance_cm();
        fprintf(stderr, "  distance: %.2f cm\n", d);
        if (hcsr04_ball_present_debounced()) {
            printf("Ball detected after pulse attempt %d/%d.\n", attempt, HOPPER_PULSE_MAX_ATTEMPTS);
            fed_ball = 1;
            break;
        }

        fprintf(stderr, "No ball detected after pulse attempt %d/%d.\n", attempt, HOPPER_PULSE_MAX_ATTEMPTS);
    }

    if (!fed_ball) {
        fprintf(stderr, "Hopper pulse: no ball detected after %d attempts.\n", HOPPER_PULSE_MAX_ATTEMPTS);
    }

    return 0;
}

void hopper_reset() {
    int sensor_aligned = 0;

    if (!motor.request) {
        hopper_alignment_required = 1;
        fprintf(stderr, "Cannot reset hopper: motor not initialized\n");
        return;
    }

    if (!tach_running) {
        hopper_alignment_required = 1;
        fprintf(stderr, "Cannot reset hopper: tachometer not initialized\n");
        return;
    }

    if (operation_interrupt_pending()) {
        hopper_alignment_required = 1;
        fprintf(stderr, "Hopper reset aborted before start due to pending interrupt.\n");
        operation_clear_interrupt();
        return;
    }

    if (hopper_running) {
        hopper_stop();
    }

    tach_gate_prepare_for_reset();

    printf("Resetting hopper: stepping until sensor trigger...\n");
    // Ungated: a reset (e.g. from homing_sequence()) must be able to run the
    // hopper stepper even if the launcher flywheel isn't running.
    hopper_start_internal();

    if (!hopper_running) {
        fprintf(stderr, "Hopper reset failed: unable to start hopper\n");
        return;
    }

    time_t start_time = time(NULL);
    struct timespec reset_started;
    clock_gettime(CLOCK_MONOTONIC, &reset_started);

    while (hopper_running) {
        if (operation_interrupt_pending()) {
            fprintf(stderr, "Hopper reset interrupted -- stopping hopper.\n");
            operation_clear_interrupt();
            break;
        }

        if (tach_gate_consume_signal()) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsed_us = (now.tv_sec - reset_started.tv_sec) * 1000000L +
                (now.tv_nsec - reset_started.tv_nsec) / 1000L;
            if (elapsed_us < HOPPER_RESET_MIN_RUN_US) {
                fprintf(stderr,
                        "Hopper reset: ignored immediate sensor trigger after %ld us.\n",
                        elapsed_us);
                tach_gate_prepare_for_reset();
                continue;
            }

            printf("Hopper reset sensor triggered; stopping in 1 second.\n");

            int run_on_elapsed_us = 0;
            while (hopper_running &&
                   run_on_elapsed_us < HOPPER_SENSOR_RUN_ON_US) {
                if (operation_interrupt_pending()) {
                    fprintf(stderr,
                            "Hopper reset interrupted during sensor run-on.\n");
                    operation_clear_interrupt();
                    break;
                }

                usleep(HOPPER_SENSOR_RUN_ON_SLICE_US);
                run_on_elapsed_us += HOPPER_SENSOR_RUN_ON_SLICE_US;
            }
            if (run_on_elapsed_us >= HOPPER_SENSOR_RUN_ON_US &&
                !operation_interrupt_pending()) {
                sensor_aligned = 1;
            }
            break;
        }

        if (difftime(time(NULL), start_time) >= HOPPER_RESET_TIMEOUT_SEC) {
            fprintf(stderr, "Hopper reset timed out after %d seconds waiting for sensor trigger -- stopping hopper.\n",
                    HOPPER_RESET_TIMEOUT_SEC);
            break;
        }

        usleep(HOPPER_RESET_POLL_DELAY_US);
    }

    hopper_stop();
    hopper_alignment_required = sensor_aligned ? 0 : 1;
    printf("Hopper reset complete.\n");
}

void hopper_mark_misaligned(void) {
    hopper_alignment_required = 1;
}

int hopper_needs_homing(void) {
    return hopper_alignment_required != 0;
}

float get_tilt_angle() {
    return curr_tilt_angle;
}

int get_speed() {
    return curr_speed;
}

int get_rpm() {
    return curr_rpm;
}

int get_hopper_pulse_count() {
    return (int)hopper_pulse_count;
}

int operation_component_status() {
    return (int)component_status_mask;
}

void pause_machine() {
    //pause the machine
    //printf("Pausing machine...\n");
    mcp4725_set_raw(&dac1, 0);

    launcher_running = 0;
    hopper_stop();

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
    int rpm = (int)get_tach_rpm();
    printf("Current RPM: %d\n", rpm);
    return rpm;
    //return 1;
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
