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

// Optional second hall sensor line used as a one-shot trigger.
// Set to -1 to disable this feature.
#ifndef TACH_GATE_LINE
#define TACH_GATE_LINE 9
#endif

// Internal bias for the gate line. Defaults to an internal pull-up because
// this second sensor is typically wired WITHOUT the external 10 kΩ
// pull-up resistor used on the primary A3144 line (see wiring notes above).
// Without a pull-up of some kind (internal or external), the line floats
// when the sensor's open-drain output isn't actively pulling it low, which
// can leave the one-shot latch permanently "stuck" (no clean rising edge
// to rearm it) -- symptom: the sensor's raw signal looks fine in isolation,
// but hopper_reset() never sees a trigger. If your second sensor DOES have
// its own external pull-up, override with -DTACH_GATE_BIAS=GPIOD_LINE_BIAS_DISABLED.
#ifndef TACH_GATE_BIAS
#define TACH_GATE_BIAS GPIOD_LINE_BIAS_PULL_UP
#endif

// Number of hall-effect magnets glued to the motor shaft per revolution.
#ifndef TACH_PULSES_PER_REV
#define TACH_PULSES_PER_REV 1
#endif

// Minimum inter-pulse gap (µs) used as a software debounce backstop.
// This is the primary (and, by default, only) debounce filter -- see
// TACH_KERNEL_DEBOUNCE_US below for why kernel-side debounce is disabled
// by default.
#define TACH_DEBOUNCE_US 1000

// Kernel-side (hardware) debounce period in microseconds, requested via
// gpiod_line_settings_set_debounce_period_us(). Disabled (0) by default.
//
// Some GPIO controllers only support a small number of coarse debounce
// steps and silently round a small request (e.g. 1000us) up to a much
// larger interval (tens to hundreds of ms). Symptom: RPM reads correctly
// at low speed, then drops straight to 0 once the true inter-pulse period
// falls below that (undocumented, rounded) interval -- e.g. a hard cutoff
// around 200 RPM. Since the software backstop above already filters
// contact-bounce noise, kernel debounce is unnecessary; only enable it
// (via -DTACH_KERNEL_DEBOUNCE_US=N) if you've confirmed your platform's
// driver honors small periods accurately.
#ifndef TACH_KERNEL_DEBOUNCE_US
#define TACH_KERNEL_DEBOUNCE_US 0
#endif

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

// Signal the tach thread(s) to stop and join them, then release the GPIO
// line request(s) and event buffer(s).
// Safe to call even if tach_init() was never called or failed.
void tach_cleanup(void);

// Thread-safe read of the current smoothed RPM value.
// Returns 0.0 if the motor is stopped or tach_init() has not been called.
float get_tach_rpm(void);

// Returns 1 exactly once per magnet approach on TACH_GATE_LINE, then resets
// to 0 until the magnet leaves (rising edge) and approaches again.
// Returns 0 when no new one-shot trigger is pending.
int tach_gate_consume_signal(void);

#endif // HAL_TACHOMETER_H