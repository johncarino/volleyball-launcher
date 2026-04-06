#include "operation.h"

// ---------------------------------------------------------------------------
// BLDC speed constants (dual-DAC, voltage-controlled ESC throttle)
// ---------------------------------------------------------------------------
#define MAX_RPM             2700.0f  // Max rated motor speed
#define DEAD_ZONE_MV        1500     // Motor doesn't spin below this voltage
#define MAX_VOLTAGE_MV      3900     // Physical ESC throttle limit (3.9 V)
#define REVERSE_SPEED_RATIO 0.4f     // Reverse motor capped at 40% of max RPM

// ---------------------------------------------------------------------------
// Launch timing
// ---------------------------------------------------------------------------
#define SPINUP_DELAY_MS     2500     // Time for flywheel to reach target RPM

// ---------------------------------------------------------------------------
// Stepper motion parameters (1/4 microstepping, 800 steps/rev)
// ---------------------------------------------------------------------------
#define YAW_START_DELAY_US  2000   // Slow start (250 steps/sec)
#define YAW_CRUISE_DELAY_US  600   // Cruise speed — bearing reduces load
#define YAW_ACCEL_STEPS      100   // Steps to ramp up/down
#define YAW_DIR_SETTLE_US  50000   // 50ms settle after direction change

// ---------------------------------------------------------------------------
// Actuator calibration coefficients — TODO: calibrate on hardware
// ---------------------------------------------------------------------------
// Tilt: linear actuator run-time per radian of angle change
static float tilt_coeff = 3000.0f;   // ms per radian  (TODO: calibrate)
// Yaw: stepper steps per radian of rotation
// 1/4 microstep (800 steps/rev): 800 / (2 * PI) = 127.32
static float yaw_coeff  = 127.32f;   // steps per radian (1/4 microstep)

// ---------------------------------------------------------------------------
// Current actuator positions (used for delta calculations)
// ---------------------------------------------------------------------------
float curr_tilt_angle = 0.0;
float curr_yaw_angle = 0.0;

// ---------------------------------------------------------------------------
// Hardware instances
// ---------------------------------------------------------------------------
tb6600_t motor;
static mcp4725_t s_dac_rev = MCP4725_INIT_ZERO;   // reverse motor DAC (0x60)
static mcp4725_t s_dac_fwd = MCP4725_INIT_ZERO;    // forward motor DAC (0x61)

// ---------------------------------------------------------------------------
// Speed-matching helper (same formula as mcp4725_test)
// ---------------------------------------------------------------------------

// Map RPM to ESC throttle voltage in millivolts.
// 0 RPM → 0 mV (off).  >0 RPM maps linearly from DEAD_ZONE_MV to MAX_VOLTAGE_MV.
static uint16_t rpm_to_mv(float rpm)
{
    if (rpm <= 0.0f) return 0;

    float mv = (float)DEAD_ZONE_MV
             + (rpm / MAX_RPM) * (float)(MAX_VOLTAGE_MV - DEAD_ZONE_MV);

    if (mv > (float)MAX_VOLTAGE_MV) mv = (float)MAX_VOLTAGE_MV;
    return (uint16_t)(mv + 0.5f);
}

// Compute the forward-motor voltage that matches the reverse motor's RPM.
// The reverse motor is limited to 40% of max speed by the ESC, so the
// forward motor needs a lower voltage to produce the same RPM.
static uint16_t compute_forward_mv(uint16_t reverse_mv)
{
    if (reverse_mv == 0) return 0;
    if (reverse_mv <= DEAD_ZONE_MV) return reverse_mv;

    float scaled = (float)DEAD_ZONE_MV
                 + ((float)reverse_mv - (float)DEAD_ZONE_MV) * REVERSE_SPEED_RATIO;
    return (uint16_t)(scaled + 0.5f);
}

void operation_init() {


    //init tb6600
    if (tb6600_init(&motor, 1) < 0) {
        fprintf(stderr, "Failed to initialize TB6600\n");
        return;
    }
    tb6600_enable(&motor, 1);

    //init mcp4725 — reverse motor DAC
    if (mcp4725_init(&s_dac_rev, MCP4725_I2C_BUS, MCP4725_I2C_ADDR) != 0) {
        fprintf(stderr, "Failed to initialize reverse DAC (0x%02x) — is I2C enabled?\n",
                MCP4725_I2C_ADDR);
        return;
    }

    //init mcp4725 — forward motor DAC
    if (mcp4725_init(&s_dac_fwd, MCP4725_I2C_BUS, MCP4725_I2C_ADDR_2) != 0) {
        fprintf(stderr, "Failed to initialize forward DAC (0x%02x) — check address\n",
                MCP4725_I2C_ADDR_2);
        mcp4725_cleanup(&s_dac_rev);
        return;
    }

    //init bts7960
    if (bts_init() != 0) {
        fprintf(stderr, "Failed to initialize BTS/BTN7960 HAL. Are you running as root?\n");
        return;
    }

    printf("Operation hardware initialized.\n");
}

void tilt_signal(float angle) {
    float delta_angle = angle - curr_tilt_angle;
    long tilt_duration = 0;
    //duty_cycle of linear actuator, as a percentage
    //keep this a constant
    int duty_cycle = 80;

    if (delta_angle == 0) {
        //no change
        return;
    }
    else if (delta_angle > 0) {
        //convert delta_angle (radians) to tilt_duration (ms)
        tilt_duration = (long)(delta_angle * tilt_coeff);
        forward_ms(duty_cycle, tilt_duration);
    }
    else { // if delta_angle < 0
        delta_angle = -delta_angle;
        //convert delta_angle (radians) to tilt_duration (ms)
        tilt_duration = (long)(delta_angle * tilt_coeff);
        reverse_ms(duty_cycle, tilt_duration);
    }
    curr_tilt_angle = angle;
}

void yaw_signal(float angle) {
    float delta_angle = angle - curr_yaw_angle;
    int yaw_steps = 0;

    if (delta_angle == 0) {
        //no change
        return;
    }
    else if (delta_angle > 0) {
        tb6600_set_direction(&motor, 1);
        //convert delta_angle (radians) to yaw steps
        yaw_steps = (int)(delta_angle * yaw_coeff);
    }
    else { // if delta_angle < 0
        tb6600_set_direction(&motor, 0);
        delta_angle = -delta_angle;
        //convert delta_angle (radians) to yaw steps
        yaw_steps = (int)(delta_angle * yaw_coeff);
    }

    // Let DIR line settle before stepping — prevents reverse-stall
    usleep(YAW_DIR_SETTLE_US);

    // Use acceleration ramp for smooth motion under load
    tb6600_step_accel(&motor, yaw_steps, YAW_START_DELAY_US,
                      YAW_CRUISE_DELAY_US, YAW_ACCEL_STEPS);

    curr_yaw_angle = angle;
}

void speed_signal(float speed) {
    // speed is rpm_output from arc_calc
    uint16_t reverse_mv = rpm_to_mv(speed);
    uint16_t forward_mv = compute_forward_mv(reverse_mv);

    mcp4725_set_mv(&s_dac_rev, reverse_mv);
    mcp4725_set_mv(&s_dac_fwd, forward_mv);

    printf("speed_signal: RPM=%.0f -> reverse=%u mV (%.2f V), forward=%u mV (%.2f V)\n",
           speed,
           reverse_mv, reverse_mv / 1000.0f,
           forward_mv, forward_mv / 1000.0f);
}
int set_machine(int set_index) {
    if (set_index < 0 || set_index >= NUM_SETS) {
        fprintf(stderr, "Invalid set index %d. Must be 0-%d.\n", set_index, NUM_SETS - 1);
        return -1;
    }

    // ----- Phase 1: Position (tilt + yaw) -----
    printf("Positioning: tilt=%.2f rad, yaw=%.2f rad\n",
           set_seq[set_index].tilt_angle,
           set_seq[set_index].yaw_angle);

    tilt_signal(set_seq[set_index].tilt_angle);
    yaw_signal(set_seq[set_index].yaw_angle);

    printf("Position set. Actuators idle.\n");

    // ----- Phase 2: Await user trigger -----
    printf("Place ball and press ENTER to launch...\n");
    // Flush any leftover newlines, then wait for ENTER
    int c;
    while ((c = getchar()) != '\n' && c != EOF) { }
    getchar();

    // ----- Phase 3: Launch (flywheel spin-up) -----
    printf("Spinning up flywheel (RPM=%.0f)...\n", set_seq[set_index].rpm_output);
    speed_signal(set_seq[set_index].rpm_output);

    // Wait for motors to reach target speed
    usleep((useconds_t)SPINUP_DELAY_MS * 1000);

    printf("Launched!\n");

    // Stop flywheel after launch
    flywheel_stop();

    return 0;
}

void flywheel_stop(void) {
    mcp4725_set_mv(&s_dac_rev, 0);
    mcp4725_set_mv(&s_dac_fwd, 0);
    printf("Flywheel stopped.\n");
}

void operation_cleanup(void) {
    // Zero out DACs (stop flywheel)
    flywheel_stop();

    // Disable and close stepper
    tb6600_enable(&motor, 0);
    tb6600_close(&motor);

    // Cleanup linear actuator PWM
    bts_cleanup();

    // Cleanup DACs
    mcp4725_cleanup(&s_dac_rev);
    mcp4725_cleanup(&s_dac_fwd);

    // Reset position tracking
    curr_tilt_angle = 0.0f;
    curr_yaw_angle  = 0.0f;

    printf("Operation hardware shut down.\n");
}