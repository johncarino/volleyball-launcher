#ifndef HCSR04_H
#define HCSR04_H

#include <stdbool.h>

/*
 * HC-SR04 ball-presence sensor, wired to gpiochip0 offset 2 (TRIG) and
 * offset 3 (ECHO). All GPIO handling and the module's one sensor instance
 * live in hcsr04.c -- callers just init/cleanup/query.
 */

/* Opens the GPIO chip and requests the TRIG/ECHO lines. Returns 0 on
 * success, -1 on failure. */
int hcsr04_init(void);

/* Releases the GPIO lines and chip handle acquired by hcsr04_init(). */
void hcsr04_cleanup(void);

/* Triggers a measurement and returns distance in cm, or a negative
 * value if the measurement timed out. */
float hcsr04_get_distance_cm(void);

/* Convenience check: true if something is within ball-detection range. */
bool hcsr04_ball_present(void);

#endif /* HCSR04_H */