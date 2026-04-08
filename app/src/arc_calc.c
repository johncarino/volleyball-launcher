#include "arc_calc.h"

// Global variable definitions
int machine_position = 0;
float machine_x[NUM_MACHINE_POSITIONS];
const float machine_y = 1.75;

int target_position = 0;
float target_x[NUM_TARGETS];
float target_y = 0.0;

int tempo_position = 0;
float peak_height[NUM_TEMPOS];

float tilt_angle[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];

float yaw_angle[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];

float launch_speed[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];

float launch_output[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];

float rpm_output[NUM_MACHINE_POSITIONS][NUM_TARGETS][NUM_TEMPOS];

void arc_calc_params(float net_height, float court_width, float court_length) {
    //launch positions (metre)
    machine_x[0] = 0 + 1; // left target
    machine_x[1] = court_width / 2; // center target
    machine_x[2] = court_width - 1; // right target

    //target positions (metre)
    target_x[0] = 0 + 0.5; // left target
    target_x[1] = court_width / 4; // left center target
    target_x[2] = court_width / 2; // center target
    target_x[3] = 3 * court_width / 4; // right center
    target_x[4] = court_width - 0.5; // right target

    target_y = net_height + 0.2;

    //tempo heights (metre)
    peak_height[0] = net_height + 0.5; // tempo 1
    peak_height[1] = net_height + 1.0; // tempo 2
    peak_height[2] = net_height + 1.5; // tempo 3
    peak_height[3] = net_height + 2.0; // tempo 4

    //court length unused
    (void)court_length;

    calculation();

    //test
    //printf("Arc calculation parameters updated based on calibration:\n");
}
//xf = target_x
//xi = machine_x

void calculation() {

    //x displacement
    float dx;
    //launch velocity
    float vy0, vx0, v0;
    //time to peak, time from peak to target, total time of flight
    float t_up, t_down, t_total;

    float theta, rpm;

    for (int i = 0; i < NUM_MACHINE_POSITIONS; i++) {
        for (int j = 0; j < NUM_TARGETS; j++) {
            dx = fabs(target_x[j] - machine_x[i]);

            for (int k = 0; k < NUM_TEMPOS; k++) {
                //vertical launch velocity calculation
                vy0 = sqrt(2*GRAVITY*(peak_height[k] - machine_y));

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
                //printf("vy0: %.2f, vx0: %.2f\n", vy0, vx0); // Debugging output
                
                theta = atan(vy0 / vx0);

                //convert to degrees
                theta = theta * 180 / M_PI;

                rpm = (v0 / (2*M_PI*WHEEL_R)) * 60 / EFF_K;

                //store results
                launch_speed[i][j][k] = v0;
                tilt_angle[i][j][k] = theta;
                rpm_output[i][j][k] = rpm;
            }
        }
    }

    yaw_calculation();
}

void yaw_calculation() {
    int left = 90;
    int right = -90;

    for (int i = 0; i < NUM_MACHINE_POSITIONS; i++) {
        if (i == 0) {
            for (int j = 0; j < NUM_TARGETS; j++) {
                for (int k = 0; k < NUM_TEMPOS; k++) {
                    yaw_angle[i][j][k] = left;
                }
            }
        }
        else if (i == NUM_MACHINE_POSITIONS - 1) {
            for (int j = 0; j < NUM_TARGETS; j++) {
                for (int k = 0; k < NUM_TEMPOS; k++) {
                    yaw_angle[i][j][k] = right;
                }
            }
        }
        else { //think of a better way to do this
            //int relative_pos = NUM_MACHINE_POSITIONS / 2 - 1;
            for (int j = 0; j < NUM_TARGETS; j++) {
                for (int k = 0; k < NUM_TEMPOS; k++) {
                    if (j < NUM_TARGETS / 2) {
                        yaw_angle[i][j][k] = left;
                    }
                    else {
                        yaw_angle[i][j][k] = right;
                    }
                }
            }
        }
    }
}

float landing_position(float xi, float yi, float theta, float rpm, float yf) {
    
    float v0, theta_rad, vx, vy;
    float a, b, c, discriminant, t1, t2, t_flight, xf;

    //convert rpm to launch speed
    v0 = 2*M_PI*WHEEL_R*(rpm * EFF_K / 60);

    //convert angle to radians
    theta_rad = theta * M_PI / 180;

    //velocity components
    vx = v0 * cos(theta_rad);
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
