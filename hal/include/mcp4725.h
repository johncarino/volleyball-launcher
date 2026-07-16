// MCP4725 12-bit DAC HAL using Linux I2C (/dev/i2c-X) on BeagleY-AI.
//
// Used to generate a 0–4.3 V analog throttle signal for the brushless DC
// motor controller (BLDC ESC) which expects an e-bike throttle input.
//
// Physical wiring (I2C1 – Motor 1):
//   SDA: GPIO14 (pin 3)
//   SCL: GPIO15 (pin 5)
//   VDD: 5 V (output range 0–5 V; scale to 0–4.3 V in software)
//   A0:  GND  → I2C address 0x60

// Physical wiring (I2C2 – Motor 2):
//   SDA: GPIO10 (pin 19)
//   SCL: GPIO11 (pin 23)
//   VDD: 5 V (output range 0–5 V; scale to 0–4.3 V in software)
//   A0:  GND  → I2C address 0x60

#ifndef HAL_MCP4725_H
#define HAL_MCP4725_H

#include <stdint.h>
#include <stdbool.h>

// Default I2C buses and address (A0 tied to GND)
#define MCP4725_I2C_BUS1 1       // Motor 1 DAC – GPIO14/15
#define MCP4725_I2C_BUS2 2       // Motor 2 DAC – GPIO10/11
#define MCP4725_I2C_ADDR 0x60

// DAC resolution
#define MCP4725_MAX_VALUE 4095   // 12-bit full-scale

// MCP4725 VDD in millivolts (determines analog output ceiling)
#define MCP4725_VDD_MV 5000

// Maximum throttle voltage the motor controller expects (mV)
#define MCP4725_THROTTLE_MAX_MV 4300

// Power-down mode selections (bits [2:1] of the command byte)
typedef enum {
    MCP4725_PD_NORMAL = 0x00,   // Normal operation
    MCP4725_PD_1K     = 0x01,   // 1 kΩ to GND
    MCP4725_PD_100K   = 0x02,   // 100 kΩ to GND
    MCP4725_PD_500K   = 0x03,   // 500 kΩ to GND
} mcp4725_pd_mode_t;

// Instance handle — one per physical MCP4725 chip.
typedef struct {
    int      fd;            // I2C file descriptor
    uint8_t  addr;          // 7-bit I2C address
    bool     initialized;   // true after successful init
} mcp4725_t;

// Convenience macro to zero-initialise a handle before first use.
#define MCP4725_INIT_ZERO { .fd = -1, .addr = 0, .initialized = false }

// Initialize the MCP4725 DAC on the given I2C bus.
// dac:  pointer to an mcp4725_t (caller owns the storage)
// bus:  Linux I2C bus number (e.g. 1 → /dev/i2c-1)
// addr: 7-bit I2C address (0x60 or 0x62)
// Returns 0 on success, -1 on failure.
int mcp4725_init(mcp4725_t *dac, int bus, uint8_t addr);

// Set the raw 12-bit DAC output value (0–4095).
// The value is written to the DAC register only (fast write, no EEPROM).
// Returns 0 on success, -1 on failure.
int mcp4725_set_raw(mcp4725_t *dac, uint16_t value);

// Set the DAC output as a voltage in millivolts (0–VDD_MV).
// Internally maps millivolts to the 12-bit DAC code.
// Returns 0 on success, -1 on failure.
int mcp4725_set_mv(mcp4725_t *dac, uint16_t millivolts);

// Set the throttle as a percentage (0–100).
// 0 %  → 0 V output
// 100% → MCP4725_THROTTLE_MAX_MV (4.3 V)
// Returns 0 on success, -1 on failure.
int mcp4725_set_throttle(mcp4725_t *dac, int percent);

// Write the current DAC value to EEPROM so it persists across power cycles.
// value: raw 12-bit code (0–4095)
// Returns 0 on success, -1 on failure.
int mcp4725_write_eeprom(mcp4725_t *dac, uint16_t value);

// Set the power-down mode (see mcp4725_pd_mode_t).
// In power-down modes the output is pulled to GND through an internal resistor.
// Returns 0 on success, -1 on failure.
int mcp4725_set_power_down(mcp4725_t *dac, mcp4725_pd_mode_t mode);

// Clean up: set output to 0 V, close the I2C file descriptor.
void mcp4725_cleanup(mcp4725_t *dac);

#endif // HAL_MCP4725_H
