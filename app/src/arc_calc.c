#include "app/src/include/arc_calc.h"

/* Global variable definitions */

int machine_position = 0;
float machine_x[NUM_MACHINE_POSITIONS];
const float machine_y = 1.55f;

int target_position = 0;
float target_x[NUM_TARGETS];
const float target_y = 0.0f;

int tempo_position = 0;
float peak_height[NUM_TEMPOS];

float tilt_angle
    [NUM_MACHINE_POSITIONS]
    [NUM_TARGETS]
    [NUM_TEMPOS];

float tilt_output
    [NUM_MACHINE_POSITIONS]
    [NUM_TARGETS]
    [NUM_TEMPOS];

float yaw_angle
    [NUM_MACHINE_POSITIONS]
    [NUM_TARGETS]
    [NUM_TEMPOS];

float yaw_output
    [NUM_MACHINE_POSITIONS]
    [NUM_TARGETS]
    [NUM_TEMPOS];

float launch_speed
    [NUM_MACHINE_POSITIONS]
    [NUM_TARGETS]
    [NUM_TEMPOS];

float launch_output
    [NUM_MACHINE_POSITIONS]
    [NUM_TARGETS]
    [NUM_TEMPOS];

float rpm_output
    [NUM_MACHINE_POSITIONS]
    [NUM_TARGETS]
    [NUM_TEMPOS];


/*
 * Compensates for weak launches at the low end of the calculated RPM range.
 */
static float apply_low_rpm_offset(float rpm)
{
    const float low_rpm_threshold = 850.0f;
    const float low_rpm_offset = 200.0f;

    if (rpm < low_rpm_threshold) {
        return rpm + low_rpm_offset;
    }

    return rpm;
}


/*
 * Marks all calculated trajectory values as invalid.
 */
static void invalidate_arc_results(void)
{
    for (int i = 0; i < NUM_MACHINE_POSITIONS; i++) {
        for (int j = 0; j < NUM_TARGETS; j++) {
            for (int k = 0; k < NUM_TEMPOS; k++) {
                tilt_angle[i][j][k] = NAN;
                launch_speed[i][j][k] = NAN;
                rpm_output[i][j][k] = NAN;
            }
        }
    }
}


/*
 * Updates the machine positions, target positions, and tempo heights.
 *
 * All distance inputs must be provided in metres.
 */
void arc_calc_params(
    float net_height,
    float court_width,
    float court_length
)
{
    if (!isfinite(net_height) || net_height <= 0.0f) {
        fprintf(
            stderr,
            "Error: net_height must be a positive finite value.\n"
        );

        invalidate_arc_results();
        return;
    }

    if (!isfinite(court_width) || court_width <= 0.0f) {
        fprintf(
            stderr,
            "Error: court_width must be a positive finite value.\n"
        );

        invalidate_arc_results();
        return;
    }

    /*
     * Machine positions in metres.
     */
    machine_x[0] = 0.84f;
    machine_x[1] = court_width / 2.0f;
    machine_x[2] = court_width - 0.84f;

    /*
     * Target positions in metres.
     */
    target_x[0] = 1.0f;
    target_x[1] = court_width / 4.0f;
    target_x[2] = court_width / 2.0f;
    target_x[3] = 3.0f * court_width / 4.0f;
    target_x[4] = court_width - 1.0f;

    /*
     * Maximum trajectory heights in metres.
     */
    peak_height[0] = net_height + 0.5f - PEAK_HEIGHT_OFFSET;
    peak_height[1] = net_height + 1.0f - PEAK_HEIGHT_OFFSET;
    peak_height[2] = net_height + 1.5f - PEAK_HEIGHT_OFFSET;
    peak_height[3] = net_height + 2.0f - PEAK_HEIGHT_OFFSET;

    /*
     * Court length is currently not used by this calculation.
     */
    (void)court_length;

    calculation();

    printf(
        "Arc calculation parameters updated based on calibration.\n"
    );
}


/*
 * Calculates the total offset from the machine reference point to the
 * ball exit point.
 *
 * dx_off is measured along the forward direction of the machine.
 * dy_off is measured vertically.
 */
void exit_point_offset(
    float theta_deg,
    float *dx_off,
    float *dy_off
)
{
    if (dx_off == NULL || dy_off == NULL) {
        fprintf(
            stderr,
            "Error: exit point output pointer is null.\n"
        );

        return;
    }

    float clamped_theta = theta_deg;

    if (!isfinite(clamped_theta)) {
        *dx_off = NAN;
        *dy_off = NAN;
        return;
    }

    if (clamped_theta < 0.0f) {
        clamped_theta = 0.0f;
    }

    if (clamped_theta > 90.0f) {
        clamped_theta = 90.0f;
    }

    const float theta_rad =
        clamped_theta * ARC_PI / 180.0f;

    *dx_off =
        PIVOT_OFFSET_X
        + ARM_LEN_A * cosf(theta_rad)
        - ARM_LEN_B * sinf(theta_rad);

    *dy_off =
        PIVOT_OFFSET_Y
        + ARM_LEN_A * sinf(theta_rad)
        + ARM_LEN_B * cosf(theta_rad);
}


/*
 * Calculates the required launch angle, launch speed, and wheel speed for
 * every machine position, target position, and tempo.
 */
void calculation(void)
{
    for (int i = 0; i < NUM_MACHINE_POSITIONS; i++) {
        for (int j = 0; j < NUM_TARGETS; j++) {
            const float dx_ref =
                fabsf(target_x[j] - machine_x[i]);

            for (int k = 0; k < NUM_TEMPOS; k++) {
                float theta = 45.0f;
                float v0 = NAN;
                int trajectory_valid = 1;

                /*
                 * The ball exit point depends on the tilt angle.
                 * The tilt angle also depends on the ball exit point.
                 *
                 * Fixed point iteration is used to solve this relationship.
                 */
                for (
                    int iter = 0;
                    iter < ARM_OFFSET_ITERATIONS;
                    iter++
                ) {
                    float dx_off;
                    float dy_off;

                    exit_point_offset(
                        theta,
                        &dx_off,
                        &dy_off
                    );

                    if (
                        !isfinite(dx_off)
                        || !isfinite(dy_off)
                    ) {
                        fprintf(
                            stderr,
                            "Invalid exit point offset at "
                            "machine %d, target %d, tempo %d.\n",
                            i,
                            j,
                            k
                        );

                        trajectory_valid = 0;
                        break;
                    }

                    /*
                     * dx_off is negative when the exit point is behind the
                     * machine reference point.
                     *
                     * Subtracting a negative offset increases the actual
                     * launch distance.
                     */
                    const float dx =
                        dx_ref - dx_off;

                    const float exit_height =
                        machine_y + dy_off;

                    const float vertical_rise =
                        peak_height[k] - exit_height;

                    const float vertical_drop =
                        peak_height[k] - target_y;

                    if (!isfinite(dx) || dx < 0.0f) {
                        fprintf(
                            stderr,
                            "Invalid horizontal distance at "
                            "machine %d, target %d, tempo %d: "
                            "dx=%.3f.\n",
                            i,
                            j,
                            k,
                            dx
                        );

                        trajectory_valid = 0;
                        break;
                    }

                    /*
                     * A square root of a negative value produces NaN.
                     *
                     * The requested peak must be above the ball exit point.
                     */
                    if (
                        !isfinite(vertical_rise)
                        || vertical_rise <= 0.0f
                    ) {
                        fprintf(
                            stderr,
                            "Invalid peak height at machine %d, "
                            "target %d, tempo %d: "
                            "peak=%.3f, exit_height=%.3f.\n",
                            i,
                            j,
                            k,
                            peak_height[k],
                            exit_height
                        );

                        trajectory_valid = 0;
                        break;
                    }

                    /*
                     * The requested peak must also be at or above the final
                     * target height.
                     */
                    if (
                        !isfinite(vertical_drop)
                        || vertical_drop < 0.0f
                    ) {
                        fprintf(
                            stderr,
                            "Invalid target height at machine %d, "
                            "target %d, tempo %d: "
                            "peak=%.3f, target_height=%.3f.\n",
                            i,
                            j,
                            k,
                            peak_height[k],
                            target_y
                        );

                        trajectory_valid = 0;
                        break;
                    }

                    /*
                     * Required vertical launch velocity.
                     */
                    const float vy0 =
                        sqrtf(
                            2.0f
                            * GRAVITY
                            * vertical_rise
                        );

                    /*
                     * Time from launch to the peak.
                     */
                    const float t_up =
                        vy0 / GRAVITY;

                    /*
                     * Time from the peak to the target.
                     */
                    const float t_down =
                        sqrtf(
                            2.0f
                            * vertical_drop
                            / GRAVITY
                        );

                    const float t_total =
                        t_up + t_down;

                    if (
                        !isfinite(t_total)
                        || t_total <= 0.0f
                    ) {
                        fprintf(
                            stderr,
                            "Invalid flight time at machine %d, "
                            "target %d, tempo %d.\n",
                            i,
                            j,
                            k
                        );

                        trajectory_valid = 0;
                        break;
                    }

                    /*
                     * Required horizontal launch velocity.
                     */
                    const float vx0 =
                        dx / t_total;

                    /*
                     * Total launch speed.
                     */
                    v0 = hypotf(vx0, vy0);

                    /*
                     * Updated launch angle in degrees.
                     */
                    theta =
                        atan2f(vy0, vx0)
                        * 180.0f
                        / ARC_PI;

                    if (
                        !isfinite(v0)
                        || !isfinite(theta)
                    ) {
                        fprintf(
                            stderr,
                            "Invalid launch result at machine %d, "
                            "target %d, tempo %d.\n",
                            i,
                            j,
                            k
                        );

                        trajectory_valid = 0;
                        break;
                    }
                }

                if (!trajectory_valid) {
                    tilt_angle[i][j][k] = NAN;
                    launch_speed[i][j][k] = NAN;
                    rpm_output[i][j][k] = NAN;
                    continue;
                }

                /*
                 * Convert linear launch speed to wheel revolutions per minute.
                 */
                const float calculated_rpm =
                    v0
                    / (
                        2.0f
                        * ARC_PI
                        * WHEEL_R
                    )
                    * 60.0f
                    / EFF_K;

                const float rpm =
                    apply_low_rpm_offset(calculated_rpm);

                if (!isfinite(rpm)) {
                    fprintf(
                        stderr,
                        "Invalid wheel speed at machine %d, "
                        "target %d, tempo %d.\n",
                        i,
                        j,
                        k
                    );

                    tilt_angle[i][j][k] = NAN;
                    launch_speed[i][j][k] = NAN;
                    rpm_output[i][j][k] = NAN;
                    continue;
                }

                tilt_angle[i][j][k] = theta;
                launch_speed[i][j][k] = v0;
                rpm_output[i][j][k] = rpm;
            }
        }
    }
}


/*
 * Calculates the world x coordinate where the ball reaches height yf.
 */
float landing_position(
    float xi,
    float yi,
    float theta,
    float rpm,
    float yf,
    float facing_dir
)
{
    if (
        !isfinite(xi)
        || !isfinite(yi)
        || !isfinite(theta)
        || !isfinite(rpm)
        || !isfinite(yf)
        || !isfinite(facing_dir)
    ) {
        fprintf(
            stderr,
            "Error: landing_position received invalid input.\n"
        );

        return NAN;
    }

    if (facing_dir == 0.0f) {
        fprintf(
            stderr,
            "Error: facing_dir cannot be zero.\n"
        );

        return NAN;
    }

    /*
     * Convert any positive value to 1 and any negative value to negative 1.
     */
    const float direction =
        facing_dir > 0.0f ? 1.0f : -1.0f;

    float dx_off;
    float dy_off;

    exit_point_offset(
        theta,
        &dx_off,
        &dy_off
    );

    if (
        !isfinite(dx_off)
        || !isfinite(dy_off)
    ) {
        fprintf(
            stderr,
            "Error: invalid exit point offset.\n"
        );

        return NAN;
    }

    /*
     * Move from the machine reference point to the actual ball exit point.
     */
    xi += dx_off * direction;
    yi += dy_off;

    /*
     * Convert wheel revolutions per minute to launch speed.
     */
    const float v0 =
        2.0f
        * ARC_PI
        * WHEEL_R
        * (
            rpm
            * EFF_K
            / 60.0f
        );

    const float theta_rad =
        theta * ARC_PI / 180.0f;

    /*
     * Velocity components in world coordinates.
     */
    const float vx =
        v0
        * cosf(theta_rad)
        * direction;

    const float vy =
        v0
        * sinf(theta_rad);

    /*
     * Solve:
     *
     * yf = yi + vy t - 0.5 g t squared
     *
     * Rearranged:
     *
     * 0.5 g t squared - vy t + yf - yi = 0
     */
    const float a =
        0.5f * GRAVITY;

    const float b =
        -vy;

    const float c =
        yf - yi;

    const float discriminant =
        b * b
        - 4.0f * a * c;

    if (
        !isfinite(discriminant)
        || discriminant < 0.0f
    ) {
        fprintf(
            stderr,
            "Error: no real solution for time of flight.\n"
        );

        return NAN;
    }

    const float sqrt_discriminant =
        sqrtf(discriminant);

    const float t1 =
        (
            -b
            + sqrt_discriminant
        )
        / (2.0f * a);

    const float t2 =
        (
            -b
            - sqrt_discriminant
        )
        / (2.0f * a);

    float t_flight = NAN;

    /*
     * When both roots are positive, the larger root represents the descending
     * point where the ball reaches the requested final height.
     */
    if (t1 > 0.0f && t2 > 0.0f) {
        t_flight = fmaxf(t1, t2);
    } else if (t1 > 0.0f) {
        t_flight = t1;
    } else if (t2 > 0.0f) {
        t_flight = t2;
    }

    if (!isfinite(t_flight)) {
        fprintf(
            stderr,
            "Error: no positive solution for time of flight.\n"
        );

        return NAN;
    }

    return xi + vx * t_flight;
}
