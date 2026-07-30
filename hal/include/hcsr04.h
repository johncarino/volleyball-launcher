#ifndef HCSR04_H
#define HCSR04_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Platform hooks the driver needs from the caller. This keeps hcsr04.c
 * free of any MCU-specific register access, so the same file drops into
 * AVR, ARM, an FPGA soft-core, etc. Fill these in with whatever your
 * platform's GPIO/timer layer provides and pass them to hcsr04_init().
 */
typedef struct {
    void (*trig_write)(bool level);       /* drive TRIG pin high/low */
    bool (*echo_read)(void);              /* read current ECHO pin level */
    void (*delay_us)(uint32_t us);        /* busy-wait delay, microseconds */
    uint32_t (*micros)(void);             /* free-running microsecond counter */
} hcsr04_hal_t;

typedef struct {
    hcsr04_hal_t hal;
    uint32_t echo_timeout_us;             /* max time to wait for echo, e.g. 30000 */
} hcsr04_t;

/* Initialize a sensor instance with the given HAL and timeout. */
void hcsr04_init(hcsr04_t *dev, const hcsr04_hal_t *hal, uint32_t echo_timeout_us);

/*
 * Release/reset a sensor instance. Drives TRIG low and clears the HAL
 * bindings so the instance can't be used again until re-initialized.
 * Call this when you're done with a sensor, e.g. before reconfiguring
 * pins or shutting down that peripheral.
 */
void hcsr04_cleanup(hcsr04_t *dev);

/*
 * Trigger a measurement and block until the echo returns or times out.
 * Returns true and writes the round-trip time in microseconds to
 * *echo_duration_us on success, false on timeout (no echo detected).
 */
bool hcsr04_measure_raw(hcsr04_t *dev, uint32_t *echo_duration_us);

/*
 * Convenience wrapper: trigger a measurement and return distance in cm.
 * Returns a negative value if the measurement timed out.
 */
float hcsr04_get_distance_cm(hcsr04_t *dev);

#endif /* HCSR04_H */