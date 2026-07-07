#include "tachometer.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>

// ---------------------------------------------------------------------------
// Shared shutdown flag – defined in operation.c, set to 1 on program exit.
// The tach thread polls this on every TACH_STALE_TIMEOUT_SEC wait cycle.
// ---------------------------------------------------------------------------
extern volatile int shutdown;

// ---------------------------------------------------------------------------
// Module-level state
// ---------------------------------------------------------------------------

pthread_t       tach_thread;
pthread_mutex_t tach_mutex  = PTHREAD_MUTEX_INITIALIZER;
volatile int    tach_running = 0;

static struct gpiod_line_request   *tach_request      = NULL;
static struct gpiod_edge_event_buffer *tach_event_buffer = NULL;

static double tach_periods[TACH_AVG_WINDOW];
static int    tach_period_count = 0;
static int    tach_period_idx   = 0;
static double tach_current_rpm  = 0.0;

// ---------------------------------------------------------------------------
// Background measurement thread
// ---------------------------------------------------------------------------

// Runs continuously for the life of the program. Watches the A3144 line for
// falling edges, keeps a rolling average over TACH_AVG_WINDOW pulses, prints
// RPM to the terminal on every pulse, and prints 0 if no pulse arrives for
// TACH_STALE_TIMEOUT_SEC seconds (motor stopped). Uses kernel-supplied edge
// timestamps rather than our own clock reads, which avoids skew from
// scheduling jitter on the calling thread.
void *tach_worker(void *arg)
{
    (void)arg;

    uint64_t last_pulse_ns = 0;
    int      have_last     = 0;
    const int64_t timeout_ns =
        (int64_t)TACH_STALE_TIMEOUT_SEC * 1000000000LL;

    while (!shutdown) {
        int wait_result = gpiod_line_request_wait_edge_events(
            tach_request, timeout_ns);

        if (wait_result < 0) {
            fprintf(stderr, "Tachometer: wait_edge_events failed\n");
            break;
        }

        if (wait_result == 0) {
            // No pulse within timeout: motor is likely stopped.
            if (have_last) {
                pthread_mutex_lock(&tach_mutex);
                tach_current_rpm  = 0.0;
                tach_period_count = 0;
                tach_period_idx   = 0;
                pthread_mutex_unlock(&tach_mutex);

                printf("Tachometer: RPM = 0.0 (no pulses for %ds)\n",
                       TACH_STALE_TIMEOUT_SEC);
                have_last = 0;
            }
            continue;
        }

        int num_events = gpiod_line_request_read_edge_events(
            tach_request, tach_event_buffer, 1);
        if (num_events < 0) {
            fprintf(stderr, "Tachometer: read_edge_events failed\n");
            break;
        }
        if (num_events == 0) {
            continue;
        }

        struct gpiod_edge_event *ev =
            gpiod_edge_event_buffer_get_event(tach_event_buffer, 0);
        if (!ev) {
            continue;
        }

        uint64_t ts_ns = gpiod_edge_event_get_timestamp_ns(ev);

        if (!have_last) {
            last_pulse_ns = ts_ns;
            have_last     = 1;
            continue;
        }

        double delta_s = (double)(ts_ns - last_pulse_ns) / 1e9;

        // Software debounce backstop (kernel debounce set in tach_init is
        // the primary filter; this catches boards where it isn't supported).
        if (delta_s * 1e6 < TACH_DEBOUNCE_US) {
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
        double avg_rpm    = (1.0 / avg_period) / TACH_PULSES_PER_REV * 60.0;
        tach_current_rpm  = avg_rpm;
        pthread_mutex_unlock(&tach_mutex);

        printf("Tachometer: RPM = %.1f (inst %.1f)\n", avg_rpm, inst_rpm);

        last_pulse_ns = ts_ns;
    }

    return NULL;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int tach_init(void)
{
    char chip_path[32];
    snprintf(chip_path, sizeof(chip_path), "/dev/gpiochip%d", TACH_GPIOCHIP);

    struct gpiod_chip *chip = gpiod_chip_open(chip_path);
    if (!chip) {
        fprintf(stderr,
                "Tachometer: failed to open %s. Run 'gpiodetect' "
                "to list available chips.\n", chip_path);
        return -1;
    }

    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    if (!settings) {
        fprintf(stderr, "Tachometer: failed to allocate line settings\n");
        gpiod_chip_close(chip);
        return -1;
    }
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_FALLING);
    // External 10 kΩ pull-up to 3.3 V per the wiring notes, so internal
    // bias is left disabled rather than fighting the external resistor.
    gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_DISABLED);
    gpiod_line_settings_set_debounce_period_us(settings, TACH_DEBOUNCE_US);

    struct gpiod_line_config *line_cfg = gpiod_line_config_new();
    if (!line_cfg) {
        fprintf(stderr, "Tachometer: failed to allocate line config\n");
        gpiod_line_settings_free(settings);
        gpiod_chip_close(chip);
        return -1;
    }

    unsigned int offset = TACH_LINE;
    if (gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings) < 0) {
        fprintf(stderr, "Tachometer: failed to add line settings\n");
        gpiod_line_config_free(line_cfg);
        gpiod_line_settings_free(settings);
        gpiod_chip_close(chip);
        return -1;
    }

    struct gpiod_request_config *req_cfg = gpiod_request_config_new();
    if (req_cfg) {
        gpiod_request_config_set_consumer(req_cfg, "dime_time_tach");
    }

    tach_request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);

    if (req_cfg)  { gpiod_request_config_free(req_cfg); }
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);
    // The request holds its own fd; the chip handle isn't needed after this.
    gpiod_chip_close(chip);

    if (!tach_request) {
        fprintf(stderr,
                "Tachometer: gpiod_chip_request_lines failed (try "
                "sudo, or add yourself to the gpio group)\n");
        return -1;
    }

    tach_event_buffer = gpiod_edge_event_buffer_new(1);
    if (!tach_event_buffer) {
        fprintf(stderr, "Tachometer: failed to allocate event buffer\n");
        gpiod_line_request_release(tach_request);
        tach_request = NULL;
        return -1;
    }

    if (pthread_create(&tach_thread, NULL, tach_worker, NULL) != 0) {
        fprintf(stderr, "Tachometer: failed to create thread\n");
        gpiod_edge_event_buffer_free(tach_event_buffer);
        tach_event_buffer = NULL;
        gpiod_line_request_release(tach_request);
        tach_request = NULL;
        return -1;
    }

    tach_running = 1;
    printf("Tachometer initialized on gpiochip%d line %d "
           "(%d pulse(s)/rev, %d-sample average)\n",
           TACH_GPIOCHIP, TACH_LINE, TACH_PULSES_PER_REV, TACH_AVG_WINDOW);
    return 0;
}

void tach_cleanup(void)
{
    if (!tach_running) {
        return;
    }

    // 'shutdown' is set by operation_cleanup() before this is called;
    // tach_worker will exit within ~TACH_STALE_TIMEOUT_SEC seconds.
    pthread_join(tach_thread, NULL);

    if (tach_event_buffer) {
        gpiod_edge_event_buffer_free(tach_event_buffer);
        tach_event_buffer = NULL;
    }
    if (tach_request) {
        gpiod_line_request_release(tach_request);
        tach_request = NULL;
    }

    tach_running = 0;
}

float get_tach_rpm(void)
{
    pthread_mutex_lock(&tach_mutex);
    float rpm = (float)tach_current_rpm;
    pthread_mutex_unlock(&tach_mutex);
    return rpm;
}