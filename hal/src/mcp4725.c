#include "hal/mcp4725.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

static int      s_fd   = -1;
static uint8_t  s_addr = 0;
static bool     s_initialized = false;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Fast-mode write: 2-byte payload, lowest latency.
static int fast_write(uint16_t value, mcp4725_pd_mode_t pd)
{
    uint8_t buf[2];

    buf[0] = (uint8_t)(((pd & 0x03) << 4) | ((value >> 8) & 0x0F));
    buf[1] = (uint8_t)(value & 0xFF);

    if (write(s_fd, buf, 2) != 2) {
        fprintf(stderr, "MCP4725 HAL: fast-write failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

// Write DAC register (3-byte command).
static int write_dac_reg(uint16_t value, mcp4725_pd_mode_t pd)
{
    uint8_t buf[3];

    buf[0] = 0x40 | ((pd & 0x03) << 1);        // C2=0 C1=1 C0=0, write DAC
    buf[1] = (uint8_t)((value >> 4) & 0xFF);    // D[11:4]
    buf[2] = (uint8_t)((value & 0x0F) << 4);    // D[3:0] in upper nibble

    if (write(s_fd, buf, 3) != 3) {
        fprintf(stderr, "MCP4725 HAL: write-dac-reg failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

// Write DAC + EEPROM (3-byte command).
static int write_dac_eeprom(uint16_t value, mcp4725_pd_mode_t pd)
{
    uint8_t buf[3];

    buf[0] = 0x60 | ((pd & 0x03) << 1);        // C2=0 C1=1 C0=1, write DAC+EEPROM
    buf[1] = (uint8_t)((value >> 4) & 0xFF);
    buf[2] = (uint8_t)((value & 0x0F) << 4);

    if (write(s_fd, buf, 3) != 3) {
        fprintf(stderr, "MCP4725 HAL: write-eeprom failed: %s\n", strerror(errno));
        return -1;
    }

    // EEPROM write cycle takes up to 50 ms.
    usleep(50000);
    return 0;
}

// Map millivolts to 12-bit DAC code, clamped to VDD.
static uint16_t mv_to_raw(uint16_t mv)
{
    if (mv >= MCP4725_VDD_MV) {
        return MCP4725_MAX_VALUE;
    }
    return (uint16_t)((uint32_t)mv * MCP4725_MAX_VALUE / MCP4725_VDD_MV);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int mcp4725_init(int bus, uint8_t addr)
{
    char dev_path[32];

    if (s_initialized) {
        fprintf(stderr, "MCP4725 HAL: already initialized\n");
        return -1;
    }

    snprintf(dev_path, sizeof(dev_path), "/dev/i2c-%d", bus);
    s_fd = open(dev_path, O_RDWR);
    if (s_fd < 0) {
        fprintf(stderr, "MCP4725 HAL: cannot open %s: %s\n", dev_path, strerror(errno));
        return -1;
    }

    if (ioctl(s_fd, I2C_SLAVE, addr) < 0) {
        fprintf(stderr, "MCP4725 HAL: ioctl I2C_SLAVE 0x%02x failed: %s\n", addr, strerror(errno));
        close(s_fd);
        s_fd = -1;
        return -1;
    }

    s_addr = addr;
    s_initialized = true;

    // Start with 0 V output.
    if (fast_write(0, MCP4725_PD_NORMAL) != 0) {
        fprintf(stderr, "MCP4725 HAL: initial zero-write failed\n");
        mcp4725_cleanup();
        return -1;
    }

    printf("MCP4725 HAL: initialized on %s, addr 0x%02x\n", dev_path, addr);
    return 0;
}

int mcp4725_set_raw(uint16_t value)
{
    if (!s_initialized) {
        fprintf(stderr, "MCP4725 HAL: not initialized\n");
        return -1;
    }
    if (value > MCP4725_MAX_VALUE) {
        value = MCP4725_MAX_VALUE;
    }
    return fast_write(value, MCP4725_PD_NORMAL);
}

int mcp4725_set_mv(uint16_t millivolts)
{
    if (!s_initialized) {
        fprintf(stderr, "MCP4725 HAL: not initialized\n");
        return -1;
    }
    return fast_write(mv_to_raw(millivolts), MCP4725_PD_NORMAL);
}

int mcp4725_set_throttle(int percent)
{
    if (!s_initialized) {
        fprintf(stderr, "MCP4725 HAL: not initialized\n");
        return -1;
    }
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    // Map 0-100 % → 0 – THROTTLE_MAX_MV, then to DAC code.
    uint16_t mv = (uint16_t)((uint32_t)percent * MCP4725_THROTTLE_MAX_MV / 100);
    return fast_write(mv_to_raw(mv), MCP4725_PD_NORMAL);
}

int mcp4725_write_eeprom(uint16_t value)
{
    if (!s_initialized) {
        fprintf(stderr, "MCP4725 HAL: not initialized\n");
        return -1;
    }
    if (value > MCP4725_MAX_VALUE) {
        value = MCP4725_MAX_VALUE;
    }
    return write_dac_eeprom(value, MCP4725_PD_NORMAL);
}

int mcp4725_set_power_down(mcp4725_pd_mode_t mode)
{
    if (!s_initialized) {
        fprintf(stderr, "MCP4725 HAL: not initialized\n");
        return -1;
    }
    return write_dac_reg(0, mode);
}

void mcp4725_cleanup(void)
{
    if (!s_initialized) { return; }

    // Drive output to 0 V before closing.
    fast_write(0, MCP4725_PD_NORMAL);

    close(s_fd);
    s_fd = -1;
    s_initialized = false;

    printf("MCP4725 HAL: cleaned up\n");
}
