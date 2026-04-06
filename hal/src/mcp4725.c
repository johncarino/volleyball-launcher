#include "hal/mcp4725.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Fast-mode write: 2-byte payload, lowest latency.
static int fast_write(const mcp4725_t *dev, uint16_t value, mcp4725_pd_mode_t pd)
{
    uint8_t buf[2];

    buf[0] = (uint8_t)(((pd & 0x03) << 4) | ((value >> 8) & 0x0F));
    buf[1] = (uint8_t)(value & 0xFF);

    if (write(dev->fd, buf, 2) != 2) {
        fprintf(stderr, "MCP4725 HAL [0x%02x]: fast-write failed: %s\n",
                dev->addr, strerror(errno));
        return -1;
    }
    return 0;
}

// Write DAC register (3-byte command).
static int write_dac_reg(const mcp4725_t *dev, uint16_t value, mcp4725_pd_mode_t pd)
{
    uint8_t buf[3];

    buf[0] = 0x40 | ((pd & 0x03) << 1);        // C2=0 C1=1 C0=0, write DAC
    buf[1] = (uint8_t)((value >> 4) & 0xFF);    // D[11:4]
    buf[2] = (uint8_t)((value & 0x0F) << 4);    // D[3:0] in upper nibble

    if (write(dev->fd, buf, 3) != 3) {
        fprintf(stderr, "MCP4725 HAL [0x%02x]: write-dac-reg failed: %s\n",
                dev->addr, strerror(errno));
        return -1;
    }
    return 0;
}

// Write DAC + EEPROM (3-byte command).
static int write_dac_eeprom(const mcp4725_t *dev, uint16_t value, mcp4725_pd_mode_t pd)
{
    uint8_t buf[3];

    buf[0] = 0x60 | ((pd & 0x03) << 1);        // C2=0 C1=1 C0=1, write DAC+EEPROM
    buf[1] = (uint8_t)((value >> 4) & 0xFF);
    buf[2] = (uint8_t)((value & 0x0F) << 4);

    if (write(dev->fd, buf, 3) != 3) {
        fprintf(stderr, "MCP4725 HAL [0x%02x]: write-eeprom failed: %s\n",
                dev->addr, strerror(errno));
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

int mcp4725_init(mcp4725_t *dev, int bus, uint8_t addr)
{
    char dev_path[32];

    if (dev == NULL) {
        fprintf(stderr, "MCP4725 HAL: NULL device pointer\n");
        return -1;
    }

    if (dev->initialized) {
        fprintf(stderr, "MCP4725 HAL [0x%02x]: already initialized\n", dev->addr);
        return -1;
    }

    snprintf(dev_path, sizeof(dev_path), "/dev/i2c-%d", bus);
    dev->fd = open(dev_path, O_RDWR);
    if (dev->fd < 0) {
        fprintf(stderr, "MCP4725 HAL: cannot open %s: %s\n", dev_path, strerror(errno));
        return -1;
    }

    if (ioctl(dev->fd, I2C_SLAVE, addr) < 0) {
        fprintf(stderr, "MCP4725 HAL: ioctl I2C_SLAVE 0x%02x failed: %s\n",
                addr, strerror(errno));
        close(dev->fd);
        dev->fd = -1;
        return -1;
    }

    dev->addr = addr;
    dev->initialized = true;

    // Start with 0 V output.
    if (fast_write(dev, 0, MCP4725_PD_NORMAL) != 0) {
        fprintf(stderr, "MCP4725 HAL [0x%02x]: initial zero-write failed\n", addr);
        mcp4725_cleanup(dev);
        return -1;
    }

    printf("MCP4725 HAL: initialized on %s, addr 0x%02x\n", dev_path, addr);
    return 0;
}

int mcp4725_set_raw(mcp4725_t *dev, uint16_t value)
{
    if (dev == NULL || !dev->initialized) {
        fprintf(stderr, "MCP4725 HAL: not initialized\n");
        return -1;
    }
    if (value > MCP4725_MAX_VALUE) {
        value = MCP4725_MAX_VALUE;
    }
    return fast_write(dev, value, MCP4725_PD_NORMAL);
}

int mcp4725_set_mv(mcp4725_t *dev, uint16_t millivolts)
{
    if (dev == NULL || !dev->initialized) {
        fprintf(stderr, "MCP4725 HAL: not initialized\n");
        return -1;
    }
    return fast_write(dev, mv_to_raw(millivolts), MCP4725_PD_NORMAL);
}

int mcp4725_set_throttle(mcp4725_t *dev, int percent)
{
    if (dev == NULL || !dev->initialized) {
        fprintf(stderr, "MCP4725 HAL: not initialized\n");
        return -1;
    }
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    // Map 0-100 % → 0 – THROTTLE_MAX_MV, then to DAC code.
    uint16_t mv = (uint16_t)((uint32_t)percent * MCP4725_THROTTLE_MAX_MV / 100);
    return fast_write(dev, mv_to_raw(mv), MCP4725_PD_NORMAL);
}

int mcp4725_write_eeprom(mcp4725_t *dev, uint16_t value)
{
    if (dev == NULL || !dev->initialized) {
        fprintf(stderr, "MCP4725 HAL: not initialized\n");
        return -1;
    }
    if (value > MCP4725_MAX_VALUE) {
        value = MCP4725_MAX_VALUE;
    }
    return write_dac_eeprom(dev, value, MCP4725_PD_NORMAL);
}

int mcp4725_set_power_down(mcp4725_t *dev, mcp4725_pd_mode_t mode)
{
    if (dev == NULL || !dev->initialized) {
        fprintf(stderr, "MCP4725 HAL: not initialized\n");
        return -1;
    }
    return write_dac_reg(dev, 0, mode);
}

void mcp4725_cleanup(mcp4725_t *dev)
{
    if (dev == NULL || !dev->initialized) { return; }

    // Drive output to 0 V before closing.
    fast_write(dev, 0, MCP4725_PD_NORMAL);

    close(dev->fd);
    dev->fd = -1;
    dev->initialized = false;

    printf("MCP4725 HAL [0x%02x]: cleaned up\n", dev->addr);
}
