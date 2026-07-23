#include "app/src/include/arc_calc.h"

// Global variable definitions
int machine_position = 0;
float machine_x[NUM_MACHINE_POSITIONS];
const float machine_y = 1.50;

int target_position = 0;
float target_x[NUM_TARGETS];
const float target_y = 0.0;

int tempo_position = 0;
float peak_height[NUM_TEMPOS];

float tilt_angle[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];
float tilt_output[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];

float yaw_angle[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];
float yaw_output[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];

float launch_speed[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];
float launch_output[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];

float rpm_output[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];

void arc_calc_params(float net_height, float court_width, float court_length) {
    //launch positions (metre)
    machine_x[0] = 0; // left target
    machine_x[1] = court_width / 2; // center target
    machine_x[2] = court_width; // right target

    //target positions (metre)
    target_x[0] = 0 + 1; // left target
    target_x[1] = court_width / 4; // left center target
    target_x[2] = 3 * court_width / 4; // right center target
    target_x[3] = court_width - 1; // right target

    //tempo heights (metre)
    peak_height[0] = net_height + 0.5; // tempo 1
    peak_height[1] = net_height + 1.0; // tempo 2
    peak_height[2] = net_height + 1.5; // tempo 3
    peak_height[3] = net_height + 2.0; // tempo 4
    peak_height[4] = net_height + 2.5; // tempo 5

    //court length unused
    (void)court_length;

    calculation();

    //test
    printf("Arc calculation parameters updated based on calibration:\n");
}
//xf = target_x
//xi = machine_x

// Total offset of the ball exit point relative to machine_x/machine_y (the
// machine reference point), as a function of tilt angle. This combines:
//   1) the fixed reference-point -> pivot offset (PIVOT_OFFSET_X/Y), and
//   2) the tilt-angle-dependent pivot -> exit-point offset, fit from measured
//      calibration data (see arc_calc.h for the raw points).
// Clamp theta to the calibrated range [0, 90] deg since the quadratic fit for
// part 2 is not valid for extrapolation outside it.
void exit_point_offset(float theta_deg, float *dx_off, float *dy_off) {
    float t = theta_deg;
    if (t < 0.0f) t = 0.0f;
    if (t > 90.0f) t = 90.0f;

    *dx_off = PIVOT_OFFSET_X + (ARM_DX_C2*t*t + ARM_DX_C1*t + ARM_DX_C0);
    *dy_off = PIVOT_OFFSET_Y + (ARM_DY_C2*t*t + ARM_DY_C1*t + ARM_DY_C0);
}

void calculation() {
    //replace with calculation logic

    //x displacement (machine reference point to target), and the offset-
    //corrected version
    float dx_ref, dx, dy_off;
    //launch velocity
    float vy0, vx0, v0;
    //time to peak, time from peak to target, total time of flight
    float t_up, t_down, t_total;

    float theta, rpm;

    for (int i = 0; i < NUM_MACHINE_POSITIONS; i++) {
        for (int j = 0; j < NUM_TARGETS; j++) {
            dx_ref = fabs(target_x[j] - machine_x[i]);

            for (int k = 0; k < NUM_TEMPOS; k++) {
                //initial guess ignoring the exit-point offset
                theta = 45.0;

                //fixed-point iteration: the exit point (and therefore dx and
                //the effective launch height) depends on theta, which is what
                //we're solving for. A handful of passes converges since the
                //offset is small relative to the target distance.
                for (int iter = 0; iter < ARM_OFFSET_ITERATIONS; iter++) {
                    exit_point_offset(theta, &dx, &dy_off);

                    //effective horizontal distance from the actual exit
                    //point to the target
                    dx = dx_ref - dx;

                    //vertical launch velocity calculation, using the actual
                    //(tilt-dependent) exit height
                    vy0 = sqrt(2*GRAVITY*(peak_height[k] - (machine_y + dy_off)));

                    //time to peak calculation
                    t_up = vy0 / GRAVITY;

                    //time from peak to target calculation
                    t_down = sqrt(2*(peak_height[k] - target_y) / GRAVITY);

                    //total time of flight calculation
                    t_total = t_up + t_down;

                    //horizontal launch velocity calculation
                    vx0 = dx / t_total;

                    //total launch speed calculation
                    v0 = sqrt(vx0*vx0 + vy0*vy0);

                    //launch angle calculation
                    theta = atan2(vy0, vx0);

                    //convert to degrees
                    theta = theta * 180 / M_PI;
                }

                rpm = (v0 / (2*M_PI*WHEEL_R)) * 60 / EFF_K;

                //store results
                launch_speed[i][j][k] = v0;
                tilt_angle[i][j][k] = theta;
                rpm_output[i][j][k] = rpm;
            }
        }
    }
}

float landing_position(float xi, float yi, float theta, float rpm, float yf, float facing_dir) {
    
    float v0, theta_rad, vx, vy;
    float a, b, c, discriminant, t1, t2, t_flight, xf;
    float dx_off, dy_off;

    //dx_off/vx are both defined in the machine's forward-facing frame
    //(positive = toward whichever target it's yawed to face). Since xi/xf
    //are world x-coordinates, that frame must be rotated into world
    //coordinates using facing_dir before combining with xi: +1.0 if the
    //machine is yawed to face +x (target at higher x than xi), -1.0 if it's
    //facing -x (target at lower x than xi). Without this, the offset and
    //velocity get applied backwards whenever the machine faces -x, since the
    //raw values from exit_point_offset()/cos(theta) assume a positive-x
    //forward direction.
    exit_point_offset(theta, &dx_off, &dy_off);
    xi = xi + dx_off * facing_dir;
    yi = yi + dy_off;

    //convert rpm to launch speed
    v0 = 2*M_PI*WHEEL_R*(rpm * EFF_K / 60);

    //convert angle to radians
    theta_rad = theta * M_PI / 180;

    //velocity components (vx rotated into world coordinates the same way)
    vx = v0 * cos(theta_rad) * facing_dir;
    vy = v0 * sin(theta_rad);

    //quadratic formula for time
    a = 0.5 * GRAVITY;
    b = -vy;
    c = yi - yf;

    discriminant = b*b - 4*a*c;

    if (discriminant < 0) {
        fprintf(stderr, "Error: No real solution for time of flight.\n");
        return -1; // error code
    }

    t1 = (-b + sqrt(discriminant)) / (2*a);
    t2 = (-b - sqrt(discriminant)) / (2*a);

    //select positive time
    t_flight = (t1 > 0) ? t1 : t2;

    //calculate landing position
    xf = xi + vx * t_flight;

    return xf;
}
