#include "hcsr04.h"
#include <stddef.h>

/* Speed of sound at ~20C, expressed as cm per microsecond of round trip. */
#define HCSR04_CM_PER_US_ROUNDTRIP 0.01715f /* 0.0343 / 2 */

#define HCSR04_TRIG_PULSE_US 10U

void hcsr04_init(hcsr04_t *dev, const hcsr04_hal_t *hal, uint32_t echo_timeout_us)
{
    dev->hal = *hal;
    dev->echo_timeout_us = echo_timeout_us;
    dev->hal.trig_write(false);
}

void hcsr04_cleanup(hcsr04_t *dev)
{
    if (dev->hal.trig_write != NULL) {
        dev->hal.trig_write(false); /* leave TRIG in a safe, known state */
    }

    /* Clear the HAL bindings so a stray call after cleanup fails loudly
     * (null pointer dereference) instead of silently touching hardware. */
    dev->hal.trig_write = NULL;
    dev->hal.echo_read  = NULL;
    dev->hal.delay_us   = NULL;
    dev->hal.micros     = NULL;
    dev->echo_timeout_us = 0;
}

bool hcsr04_measure_raw(hcsr04_t *dev, uint32_t *echo_duration_us)
{
    const hcsr04_hal_t *hal = &dev->hal;

    /* Send the 10us trigger pulse to start a measurement. */
    hal->trig_write(false);
    hal->delay_us(2);
    hal->trig_write(true);
    hal->delay_us(HCSR04_TRIG_PULSE_US);
    hal->trig_write(false);

    /* Wait for ECHO to go high, bounded by the timeout. */
    uint32_t wait_start = hal->micros();
    while (!hal->echo_read()) {
        if ((hal->micros() - wait_start) > dev->echo_timeout_us) {
            return false; /* echo never started, likely no sensor response */
        }
    }

    /* Measure how long ECHO stays high, again bounded by the timeout. */
    uint32_t pulse_start = hal->micros();
    while (hal->echo_read()) {
        if ((hal->micros() - pulse_start) > dev->echo_timeout_us) {
            return false; /* echo stuck high, treat as a failed reading */
        }
    }
    uint32_t pulse_end = hal->micros();

    *echo_duration_us = pulse_end - pulse_start;
    return true;
}

float hcsr04_get_distance_cm(hcsr04_t *dev)
{
    uint32_t duration_us;

    if (!hcsr04_measure_raw(dev, &duration_us)) {
        return -1.0f;
    }

    return (float)duration_us * HCSR04_CM_PER_US_ROUNDTRIP;
}