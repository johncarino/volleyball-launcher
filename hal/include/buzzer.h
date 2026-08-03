#ifndef BUZZER_H
#define BUZZER_H

// Active buzzer output on chip 1 / offset 33.
// Sounds the buzzer for duration_ms milliseconds (blocking).
void buzzer_tone(int duration_ms);

// Releases the GPIO line/chip. Safe to call even if buzzer_tone() was
// never called (i.e. the buzzer was never initialized).
void buzzer_cleanup(void);

#endif // BUZZER_H