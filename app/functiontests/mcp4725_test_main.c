// MCP4725 DAC functional test
//
// Verifies the MCP4725 HAL by stepping through a series of output levels
// and exercising the main API calls:
//   1. Init / cleanup
//   2. Raw 12-bit write
//   3. Millivolt write
//   4. Throttle percentage write
//   5. Keyboard throttle percentage write (0–3.9 V cap)
//   6. EEPROM write
//   7. Power-down mode

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>

#include "hal/mcp4725.h"

#if defined(__GNUC__)
#define MAYBE_UNUSED __attribute__((unused))
#else
#define MAYBE_UNUSED
#endif

static volatile bool s_running = true;
static const uint16_t THROTTLE_KEYBOARD_MAX_MV = 3900;

static void signal_handler(int sig)
{
    (void)sig;
    s_running = false;
}

// Helper: pause between tests so you can probe with a multimeter.
static MAYBE_UNUSED void pause_ms(int ms)
{
    usleep(ms * 1000);
}

// ---------------------------------------------------------------------------
// Individual tests
// ---------------------------------------------------------------------------

static MAYBE_UNUSED int test_raw_output(void)
{
    printf("\n--- Test 1: Raw 12-bit output (ramp 0 → 4095) ---\n");

    const uint16_t steps[] = { 0, 512, 1024, 2048, 3072, 4095 };
    const int n = (int)(sizeof(steps) / sizeof(steps[0]));

    for (int i = 0; i < n && s_running; i++) {
        printf("  raw = %4u  (expected ≈ %.2f V)\n",
               steps[i],
               (float)steps[i] * MCP4725_VDD_MV / MCP4725_MAX_VALUE / 1000.0f);
        if (mcp4725_set_raw(steps[i]) != 0) {
            fprintf(stderr, "  FAIL: mcp4725_set_raw(%u)\n", steps[i]);
            return -1;
        }
        pause_ms(1500);
    }
    printf("  PASS\n");
    return 0;
}

static MAYBE_UNUSED int test_mv_output(void)
{
    printf("\n--- Test 2: Millivolt output ---\n");

    const uint16_t mv_steps[] = { 0, 1000, 2000, 3000, 4000, 4300 };
    const int n = (int)(sizeof(mv_steps) / sizeof(mv_steps[0]));

    for (int i = 0; i < n && s_running; i++) {
        printf("  target = %u mV\n", mv_steps[i]);
        if (mcp4725_set_mv(mv_steps[i]) != 0) {
            fprintf(stderr, "  FAIL: mcp4725_set_mv(%u)\n", mv_steps[i]);
            return -1;
        }
        pause_ms(1500);
    }
    printf("  PASS\n");
    return 0;
}

static MAYBE_UNUSED int test_throttle(void)
{
    printf("\n--- Test 3: Throttle percentage (0%% → 100%%) ---\n");

    for (int pct = 0; pct <= 100 && s_running; pct += 10) {
        float expected_v = (float)pct * MCP4725_THROTTLE_MAX_MV / 100.0f / 1000.0f;
        printf("  throttle = %3d%%  (expected ≈ %.2f V)\n", pct, expected_v);
        if (mcp4725_set_throttle(pct) != 0) {
            fprintf(stderr, "  FAIL: mcp4725_set_throttle(%d)\n", pct);
            return -1;
        }
        pause_ms(1000);
    }
    printf("  PASS\n");
    return 0;
}

static int test_keyboard_throttle(void)
{
    char input[64];

    printf("\n--- Test 4: Keyboard throttle control (0-100%%, q to quit) ---\n");
    printf("    100%% maps to %u mV (%.2f V max)\n",
           THROTTLE_KEYBOARD_MAX_MV,
           (float)THROTTLE_KEYBOARD_MAX_MV / 1000.0f);

    while (s_running) {
        printf("  Enter throttle %% [0-100] or q: ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) {
            return 0;
        }

        if (input[0] == 'q' || input[0] == 'Q') {
            break;
        }

        errno = 0;
        char *endptr = NULL;
        long pct = strtol(input, &endptr, 10);

        if (errno != 0 || endptr == input) {
            printf("  Invalid input. Enter a number or q.\n");
            continue;
        }

        while (*endptr == ' ' || *endptr == '\t') {
            endptr++;
        }
        if (*endptr != '\n' && *endptr != '\0') {
            printf("  Invalid input. Enter a number or q.\n");
            continue;
        }

        if (pct < 0) {
            pct = 0;
        }
        if (pct > 100) {
            printf("  Clamped to 100%% max.\n");
            pct = 100;
        }

        uint16_t mv = (uint16_t)((uint32_t)pct * THROTTLE_KEYBOARD_MAX_MV / 100U);

        if (mcp4725_set_mv(mv) != 0) {
            fprintf(stderr, "  FAIL: mcp4725_set_mv(%u)\n", mv);
            return -1;
        }

        printf("  Applied throttle = %ld%% -> %u mV (%.2f V)\n",
               pct,
               mv,
               (float)mv / 1000.0f);
    }

    printf("  PASS\n");
    return 0;
}

static MAYBE_UNUSED int test_eeprom(void)
{
    printf("\n--- Test 5: EEPROM write (mid-scale 2048) ---\n");

    if (mcp4725_write_eeprom(2048) != 0) {
        fprintf(stderr, "  FAIL: mcp4725_write_eeprom(2048)\n");
        return -1;
    }
    printf("  Wrote 2048 to EEPROM — power-cycle the DAC and verify ~2.5 V output.\n");
    printf("  PASS\n");
    return 0;
}

static MAYBE_UNUSED int test_power_down(void)
{
    printf("\n--- Test 6: Power-down modes ---\n");

    const mcp4725_pd_mode_t modes[] = {
        MCP4725_PD_1K, MCP4725_PD_100K, MCP4725_PD_500K, MCP4725_PD_NORMAL
    };
    const char *labels[] = { "1kΩ", "100kΩ", "500kΩ", "Normal" };

    for (int i = 0; i < 4 && s_running; i++) {
        printf("  mode: %s\n", labels[i]);
        if (mcp4725_set_power_down(modes[i]) != 0) {
            fprintf(stderr, "  FAIL: mcp4725_set_power_down(%s)\n", labels[i]);
            return -1;
        }
        pause_ms(1500);
    }
    printf("  PASS\n");
    return 0;
}

int main(void)
{
    printf("=== MCP4725 DAC Functional Test ===\n");
    printf("Bus: /dev/i2c-%d  Addr: 0x%02x\n", MCP4725_I2C_BUS, MCP4725_I2C_ADDR);
    printf("VDD: %d mV   Throttle max: %d mV\n\n", MCP4725_VDD_MV, MCP4725_THROTTLE_MAX_MV);

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    if (mcp4725_init(MCP4725_I2C_BUS, MCP4725_I2C_ADDR) != 0) {
        fprintf(stderr, "Failed to initialize MCP4725 — is I2C enabled?\n");
        return -1;
    }

    int rc = 0;
    // Keep full suite available, but run only keyboard mode for now:
    // if (s_running && (rc = test_raw_output())   != 0) goto done;
    // if (s_running && (rc = test_mv_output())    != 0) goto done;
    // if (s_running && (rc = test_throttle())     != 0) goto done;
    if (s_running && (rc = test_keyboard_throttle()) != 0) goto done;
    // if (s_running && (rc = test_eeprom())       != 0) goto done;
    // if (s_running && (rc = test_power_down())   != 0) goto done;

done:
    // Always return output to 0 V and clean up.
    mcp4725_set_raw(0);
    mcp4725_cleanup();

    if (rc == 0 && s_running) {
        printf("\n=== ALL TESTS PASSED ===\n");
    } else if (!s_running) {
        printf("\n=== INTERRUPTED — cleaned up ===\n");
    } else {
        printf("\n=== TEST FAILED ===\n");
    }
    return rc;
}
