// MCP4725 dual-DAC test — volleyball launcher speed matching
//
// Two BLDC motors driven by independent MCP4725 DACs:
//   - Reverse motor (DAC @ 0x60): receives the user-specified voltage
//   - Forward motor (DAC @ 0x61): scaled down so its RPM matches the
//     reverse motor, which is hardware-limited to 40% of max speed.
//
// The user enters a voltage (0.0–3.9 V) and the program computes the
// matching forward voltage using a dead-zone-aware formula:
//
//   forward_mV = DEAD_ZONE_MV + (reverse_mV - DEAD_ZONE_MV) * 0.4
//
// The BLDC controllers have a ~1.5 V dead zone (motor doesn't spin below
// that voltage).  Entering 0 V turns both motors off.

#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>

#include "hal/mcp4725.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

#define MAX_VOLTAGE_V       3.9f    // Physical limit of ESC throttle input
#define DEAD_ZONE_MV        1500    // Motor doesn't spin below this voltage
#define REVERSE_SPEED_RATIO 0.4f    // Reverse motor is capped at 40% of max RPM

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

static volatile bool s_running = true;
static mcp4725_t dac_reverse = MCP4725_INIT_ZERO;  // 0x60
static mcp4725_t dac_forward = MCP4725_INIT_ZERO;  // 0x61

static void signal_handler(int sig)
{
    (void)sig;
    s_running = false;
}

// ---------------------------------------------------------------------------
// Speed-matching helper
// ---------------------------------------------------------------------------

// Compute the forward-motor voltage that matches the reverse motor's RPM.
//
// Because the reverse motor is limited to 40 % of max speed by the ESC,
// at any given voltage above the dead-zone it produces only 40 % of the
// RPM that the forward motor would.  To equalise:
//
//   forward_mV = DEAD_ZONE_MV + (reverse_mV - DEAD_ZONE_MV) * REVERSE_SPEED_RATIO
//
// Special cases:
//   reverse_mV == 0         → 0 (both off)
//   reverse_mV <= dead zone → same voltage (neither motor spins yet)
static uint16_t compute_forward_mv(uint16_t reverse_mv)
{
    if (reverse_mv == 0) {
        return 0;
    }
    if (reverse_mv <= DEAD_ZONE_MV) {
        return reverse_mv;
    }

    float scaled = (float)DEAD_ZONE_MV
                 + ((float)reverse_mv - (float)DEAD_ZONE_MV) * REVERSE_SPEED_RATIO;
    return (uint16_t)(scaled + 0.5f);   // round to nearest mV
}

// ---------------------------------------------------------------------------
// Interactive voltage test
// ---------------------------------------------------------------------------

static int test_keyboard_voltage(void)
{
    char input[64];

    printf("\n--- Dual-DAC Keyboard Voltage Control ---\n");
    printf("  Accepted range : 0.0 – %.1f V\n", MAX_VOLTAGE_V);
    printf("  Dead zone      : %.1f V (motor starts spinning above this)\n",
           DEAD_ZONE_MV / 1000.0f);
    printf("  Reverse motor  : receives your voltage directly\n");
    printf("  Forward motor  : scaled to match reverse RPM (×%.0f%%)\n",
           REVERSE_SPEED_RATIO * 100.0f);
    printf("  Enter 0 to turn both motors off, q to quit.\n");

    while (s_running) {
        printf("\n  Voltage [0.0–%.1f] or q: ", MAX_VOLTAGE_V);
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) {
            return 0;
        }

        if (input[0] == 'q' || input[0] == 'Q') {
            break;
        }

        errno = 0;
        char *endptr = NULL;
        float voltage = strtof(input, &endptr);

        if (errno != 0 || endptr == input) {
            printf("  Invalid input. Enter a voltage or q.\n");
            continue;
        }

        // Skip trailing whitespace
        while (*endptr == ' ' || *endptr == '\t') {
            endptr++;
        }
        if (*endptr != '\n' && *endptr != '\0') {
            printf("  Invalid input. Enter a voltage or q.\n");
            continue;
        }

        // Clamp
        if (voltage < 0.0f) {
            voltage = 0.0f;
        }
        if (voltage > MAX_VOLTAGE_V) {
            printf("  Clamped to %.1f V max.\n", MAX_VOLTAGE_V);
            voltage = MAX_VOLTAGE_V;
        }

        uint16_t reverse_mv = (uint16_t)(voltage * 1000.0f + 0.5f);
        uint16_t forward_mv = compute_forward_mv(reverse_mv);

        if (mcp4725_set_mv(&dac_reverse, reverse_mv) != 0) {
            fprintf(stderr, "  FAIL: dac_reverse set_mv(%u)\n", reverse_mv);
            return -1;
        }
        if (mcp4725_set_mv(&dac_forward, forward_mv) != 0) {
            fprintf(stderr, "  FAIL: dac_forward set_mv(%u)\n", forward_mv);
            return -1;
        }

        printf("  Reverse: %u mV (%.2f V)  |  Forward: %u mV (%.2f V)\n",
               reverse_mv, reverse_mv / 1000.0f,
               forward_mv, forward_mv / 1000.0f);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void)
{
    printf("=== MCP4725 Dual-DAC Volleyball Launcher Test ===\n");
    printf("Reverse DAC : /dev/i2c-%d  addr 0x%02x\n",
           MCP4725_I2C_BUS, MCP4725_I2C_ADDR);
    printf("Forward DAC : /dev/i2c-%d  addr 0x%02x\n",
           MCP4725_I2C_BUS, MCP4725_I2C_ADDR_2);
    printf("VDD: %d mV   Max input: %.1f V   Dead zone: %.1f V\n\n",
           MCP4725_VDD_MV, MAX_VOLTAGE_V, DEAD_ZONE_MV / 1000.0f);

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    // Init reverse motor DAC (0x60)
    if (mcp4725_init(&dac_reverse, MCP4725_I2C_BUS, MCP4725_I2C_ADDR) != 0) {
        fprintf(stderr, "Failed to init reverse DAC (0x%02x) — is I2C enabled?\n",
                MCP4725_I2C_ADDR);
        return 1;
    }

    // Init forward motor DAC (0x61)
    if (mcp4725_init(&dac_forward, MCP4725_I2C_BUS, MCP4725_I2C_ADDR_2) != 0) {
        fprintf(stderr, "Failed to init forward DAC (0x%02x) — check address with i2cdetect -y 1\n",
                MCP4725_I2C_ADDR_2);
        mcp4725_cleanup(&dac_reverse);
        return 1;
    }

    int rc = test_keyboard_voltage();

    // Always return both outputs to 0 V and clean up.
    mcp4725_set_raw(&dac_reverse, 0);
    mcp4725_set_raw(&dac_forward, 0);
    mcp4725_cleanup(&dac_reverse);
    mcp4725_cleanup(&dac_forward);

    if (rc == 0 && s_running) {
        printf("\n=== TEST COMPLETE — both DACs at 0 V ===\n");
    } else if (!s_running) {
        printf("\n=== INTERRUPTED — both DACs cleaned up ===\n");
    } else {
        printf("\n=== TEST FAILED ===\n");
    }
    return rc;
}
