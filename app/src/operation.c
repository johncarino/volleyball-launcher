#include "operation.h"

// ---------------------------------------------------------------------------
// BLDC speed constants (dual-DAC, voltage-controlled ESC throttle)
// ---------------------------------------------------------------------------
#define MAX_RPM             2700.0f  // Max rated motor speed
#define DEAD_ZONE_MV        1500     // Motor doesn't spin below this voltage
#define MAX_VOLTAGE_MV      3900     // Physical ESC throttle limit (3.9 V)
#define REVERSE_SPEED_RATIO 0.4f     // Reverse motor capped at 40% of max RPM

// ---------------------------------------------------------------------------
// Actuator calibration coefficients — TODO: calibrate on hardware
// ---------------------------------------------------------------------------
// Tilt: linear actuator run-time per radian of angle change
static float tilt_coeff = 3000.0f;   // ms per radian  (TODO: calibrate)
// Yaw: stepper steps per radian of rotation
static float yaw_coeff  = 130.0f;    // steps per radian (TODO: calibrate)

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

    set_machine(0);
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
    bts_cleanup();

    curr_tilt_angle = angle;
}

void yaw_signal(float angle) {
    float delta_angle = angle - curr_yaw_angle;
    int yaw_steps = 0;
    //delay of stepper motor, in us
    //keep this a constant
    int delay = 500;

    if (delta_angle == 0) {
        //no change
        return;
    }
    else if (delta_angle > 0) {
        tb6600_set_direction(&motor, 1);
        //convert delta_angle (radians) to yaw steps
        yaw_steps = (int)(delta_angle * yaw_coeff);
        tb6600_step(&motor, yaw_steps, delay);
    }
    else { // if delta_angle < 0
        tb6600_set_direction(&motor, 0);
        delta_angle = -delta_angle;
        //convert delta_angle (radians) to yaw steps
        yaw_steps = (int)(delta_angle * yaw_coeff);
        tb6600_step(&motor, yaw_steps, delay);
    }

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
void set_machine(int set_index) {
    //start the machine for set index
    //run in parallel?
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