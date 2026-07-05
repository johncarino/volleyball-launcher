// A3144 hall-effect tachometer HAL for BLDC RPM measurement.
//
// Listens for falling edges on a GPIO input connected to an A3144 hall
// sensor (5 V supply, 10 kΩ pull-up to 3.3 V on OUT). Computes
// instantaneous and rolling-average RPM from kernel edge timestamps and
// reports them via printf. Thread-safe getter available for closed-loop use.
//
// Requires libgpiod v2 (gpiod_chip_request_lines / gpiod_line_request_*).
// Check installed version with: pkg-config --modversion libgpiod
//
// Physical wiring:
//   A3144 VCC  → 5 V
//   A3144 GND  → GND
//   A3144 OUT  → 10 kΩ pull-up to 3.3 V, then to GPIO input line

#ifndef HAL_TACHOMETER_H
#define HAL_TACHOMETER_H

#include <gpiod.h>
#include <pthread.h>

// ---------------------------------------------------------------------------
// Configuration constants – override at compile time with -DTACH_GPIOCHIP=N
// ---------------------------------------------------------------------------

// GPIO chip number: the N in /dev/gpiochipN. Run 'gpiodetect' to list chips.
#ifndef TACH_GPIOCHIP
#define TACH_GPIOCHIP 0
#endif

// Line offset on the chip connected to the A3144 OUT pin.
// Run 'gpioinfo gpiochipN' to find the offset for your pin.
#ifndef TACH_LINE
#define TACH_LINE 0
#endif

// Number of hall-effect magnets glued to the motor shaft per revolution.
#ifndef TACH_PULSES_PER_REV
#define TACH_PULSES_PER_REV 1
#endif

// Minimum inter-pulse gap (µs) used as a software debounce backstop.
// The kernel debounce (set in tach_init) is the primary filter.
#define TACH_DEBOUNCE_US 1000

// Seconds without a pulse before RPM is reported as 0 (motor stopped).
#define TACH_STALE_TIMEOUT_SEC 2

// Number of consecutive pulse periods to include in the rolling average.
#define TACH_AVG_WINDOW 5

// ---------------------------------------------------------------------------
// External state – owned by tachometer.c, exposed for callers that need
// direct access (e.g. joining the thread from a signal handler).
// ---------------------------------------------------------------------------

extern pthread_t       tach_thread;
extern pthread_mutex_t tach_mutex;
extern volatile int    tach_running;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Open the GPIO chip, configure the line for falling-edge input with
// debounce, allocate the event buffer, and start the background measurement
// thread. Call once, typically from operation_init().
// Returns 0 on success, -1 on failure (error printed to stderr).
int  tach_init(void);

// Signal the tach thread to stop and join it, then release the GPIO line
// request and event buffer. Relies on the global 'shutdown' flag being set
// to 1 before calling (same convention as the tilt/yaw workers).
// Safe to call even if tach_init() was never called or failed.
void tach_cleanup(void);

// Thread-safe read of the current smoothed RPM value.
// Returns 0.0 if the motor is stopped or tach_init() has not been called.
float get_tach_rpm(void);

#endif // HAL_TACHOMETER_H
