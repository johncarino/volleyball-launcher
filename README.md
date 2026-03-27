# Volleyball Launcher — Programmable Trajectory System

An automated volleyball launcher that computes and executes variable-angle, variable-speed shots. Controllable via onboard calibration and targeting modes. Runs on a **BeagleY-AI**.

## Hardware Components

**Main processor:**
- **BeagleY-AI** (TI AM67A ARM64)

**Actuation subsystems:**
- **BLDC Motor (x2):** VEVOR 500W brushless DC, 48V, 2700 rpm (launch motors)
- **Linear Actuator:** BTS7960 H-bridge PWM controller (tilt/angle adjustment)
- **Stepper Motor:** TB6600 driver (yaw/rotation adjustment)

**Power & control:**
- **Power supply:** 48V DC
- **I2C DAC:** MCP4725 12-bit (throttle signal 0–4.3 V for BLDC ESC)
- **PWM generator:** Linux sysfs on EPWM0 (GPIO12, GPIO15)
- **GPIO:** libgpiod for TB6600 stepper control

### Wiring

**BLDC launcher motors (PWM-controlled via MCP4725 DAC):**
- I2C 1 (SDA: GPIO14 pin 3, SCL: GPIO15 pin 5) → MCP4725 A0 address 0x60 → ESC throttle input
- ESC outputs to 2x VEVOR 500W motors

**Linear actuator (BTS7960):**
- GPIO12 (pin 32) → pwmchip3/pwm1 → RPWM (forward)
- GPIO15 (pin 10) → pwmchip3/pwm0 → LPWM (reverse)
- Approx 10mm per ~300 ms at 70% duty cycle

**Stepper (TB6600):**
- gpiochip0 → STEP, DIR, EN pins → TB6600 → stepper motor
- Rotation increments on-demand via STEP pulses (500 µs delay)

## Project Structure

```
.
├── app/
│   ├── CMakeLists.txt                  # App build targets
│   ├── functiontests/
│   │   ├── bts7960_test_main.c        # Linear actuator functional test
│   │   ├── mcp4725_test_main.c        # BLDC throttle DAC functional test
│   │   ├── tb6600_test_main.c         # Stepper motor functional test
│   │   └── system_integration_test_main.c  # Full system interactive test
│   └── src/
│       ├── include/
│       │   ├── fsm.h                  # Finite state machine
│       │   ├── calibration.h          # Court/net calibration
│       │   ├── arc_calc.h             # Physics/trajectory solver
│       │   ├── set.h                  # Preset set manager
│       │   ├── operation.h            # Hardware execution layer
│       │   └── advanced.h             # Advanced features (future)
│       ├── fsm_main.c                 # FSM application entry point
│       ├── fsm.c                      # FSM state management
│       ├── calibration.c              # Court parameter tuning
│       ├── arc_calc.c                 # Physics calculations (angles, speeds)
│       ├── set.c                      # Set sequence storage/retrieval
│       ├── operation.c                # HAL command execution (tilt/yaw/speed)
│       └── advanced.c                 # Advanced features placeholder
├── hal/
│   ├── CMakeLists.txt                 # HAL build targets
│   ├── include/hal/
│   │   ├── pwm.h                      # PWM control (BTS7960 interface)
│   │   ├── bts7960.h                  # Linear actuator driver HAL
│   │   ├── mcp4725.h                  # BLDC throttle DAC HAL
│   │   └── tb6600.h                   # Stepper motor driver HAL
│   └── src/
│       ├── pwm.c                      # Linux sysfs PWM implementation
│       ├── bts7960.c                  # BTS7960 forward/reverse control
│       ├── mcp4725.c                  # I2C DAC voltage control
│       └── tb6600.c                   # TB6600 step/direction control
├── CMakeLists.txt                     # Top-level build configuration
└── README.md
```

**Key architecture:**
- **HAL layer** (`hal/`) — abstraction for GPIO, PWM, I2C, abstracting hardware details
- **Application layer** (`app/src/`) — FSM state management, physics, trajectory calculation, preset storage
- **Operation layer** (`app/src/operation.c`) — translates user commands (angles, speeds) into HAL calls

## BeagleY-AI Setup & Enablement

### PWM Overlay (EPWM0 for BTS7960)

Edit the boot config:

```bash
sudo nano /boot/firmware/extlinux/extlinux.conf
```

Add this line under the `label microSD (default)` section:

```
fdtoverlays /overlays/k3-am67a-beagley-ai-pwm-epwm0-gpio12-gpio15.dtbo
```

Reboot for the overlay to take effect.

### I2C Enablement (MCP4725 DAC)

I2C1 is typically enabled by default on BeagleY-AI. Verify:

```bash
# Check I2C bus 1 exists
ls /dev/i2c-1

# Optionally scan for MCP4725 at address 0x60
sudo i2cdetect -y 1
```

If missing, enable I2C1 overlay in extlinux.conf and reboot.

### GPIO Verification (TB6600 Stepper)

`libgpiod` accesses GPIO via `/dev/gpiochip0`. Verify availability:

```bash
ls /dev/gpiochip0
```

If permission denied, ensure you run stepper tests with `sudo`.

## Building

### Prerequisites

- CMake (`sudo apt install cmake`)
- ARM64 cross-compiler (`sudo apt install gcc-aarch64-linux-gnu`)
- VS Code with **CMake Tools** extension

### Cross-Compile for BeagleY-AI

1. Delete the `build/` folder: `rm -rf build`
2. In VS Code: **Ctrl+Shift+P → CMake: Select a Kit** → choose `aarch64-linux-gnu-gcc`
3. **Ctrl+Shift+P → CMake: Configure**
4. Build with **Ctrl+Shift+B** or **Ctrl+Shift+P → CMake: Build**

The executable is automatically copied to `/home/john/Desktop/BeagleY-AI/public/motor_control`.

### Manual Build (command line)

```bash
rm -rf build/
cmake -S . -B build
cmake --build build
```

## Running on the BeagleY-AI

The main application executable is `motor_control` (FSM-based). Run with `sudo`:

```bash
sudo ./motor_control
```

**Main FSM entry points:**
1. **Calibration mode** — adjust net height and court dimensions
2. **Set mode** — build presets (select position/target/tempo, system calculates launch params)
3. **Operation mode** — execute saved set sequences, repeat, or shuffle
4. **Ctrl+C** — clean shutdown, all systems idle

## Application Overview

### Finite State Machine (FSM)

The application runs as a state machine with four modes:

1. **Calibration Mode** — User adjusts court parameters (net height, court width/length)
   - Triggers physics recalculation across all machine positions, targets, and tempos
   - Stores calibration in global state

2. **Set Mode** — User builds preset "sets" (launch sequences)
   - Choose: machine position (left/center/right), target (L/LC/C/RC/R), tempo (1/2/3/4)
   - Physics lookup table automatically selects launch angle, speed, RPM for the combo
   - Save up to 4 preset sets for quick recall

3. **Advanced Mode** — Manual angle/speed entry (future feature)
   - Specify exact launch angle, speed, rpm by hand

4. **Operation Mode** — Execute saved sets
   - Run presets in sequence, repeat, or shuffle
   - Clean abort to idle state

### Physics Core (`arc_calc.c`)

Given court geometry, computes required launch parameters for all combinations:
- **Input:** machine position, target location, tempo (peak height)
- **Output:** launch angle (tilt), horizontal/vertical velocity, motor RPM
- **Calculation:** ballistic trajectory solver using quadratic time-of-flight equations
- **Storage:** 3D lookup table indexed by [machine pos][target][tempo]

Generates launch_speed, tilt_angle, yaw_angle, rpm_output arrays for fast real-time lookup.

### Hardware Execution (`operation.c`)

Translates abstract angles and speeds into HAL commands:
- `tilt_signal(angle)` — moves linear actuator to target angle via BTS7960
- `yaw_signal(angle)` — rotates launcher to target bearing via TB6600 stepper
- `speed_signal(speed)` — sets BLDC throttle voltage via MCP4725 DAC

## HAL API Reference

### MCP4725 DAC (BLDC Throttle)

| Function | Purpose |
|----------|---------|
| `mcp4725_init(bus, addr)` | Initialize I2C connection to DAC |
| `mcp4725_set_mv(millivolts)` | Set output voltage (0–4300 mV recommended for BLDC ESC) |
| `mcp4725_set_throttle(percent)` | Set as percentage (0–100%) → 0–4.3 V |
| `mcp4725_cleanup()` | Close I2C, reset to 0 V |

**Config constants:**
- `MCP4725_THROTTLE_MAX_MV` = 4300 mV (ESC cutoff at ~3.9 V, motor stall-spin at ~1.5 V)
- Default frequency applied: 1 kHz

### BTS7960 Linear Actuator (Tilt)

| Function | Purpose |
|----------|---------|
| `bts_init()` | Initialize PWM channels for forward/reverse |
| `forward_ms(percent, milliseconds)` | Extend actuator at given duty cycle for duration |
| `reverse_ms(percent, milliseconds)` | Retract actuator at given duty cycle for duration |
| `bts_cleanup()` | Disable PWM, reset pins |

**Behavior:** ~10 mm per 300 ms at 70% duty; duration is time-based, not distance feedback.

### TB6600 Stepper Motor (Yaw/Rotation)

| Function | Purpose |
|----------|---------|
| `tb6600_init(motor, use_enable)` | Acquire GPIO lines, enable optional EN pin |
| `tb6600_set_direction(motor, dir)` | Set rotation direction (0=CCW, 1=CW) |
| `tb6600_step(motor, steps, delay_us)` | Generate pulse train for N steps with inter-pulse delay |
| `tb6600_enable(motor, enable)` | Control EN output (if configured) |
| `tb6600_close(motor)` | Release GPIO resources |

**Behavior:** Stepper provides full rotation control; pulse delay in microseconds controls speed.

### PWM HAL (shared, low-level)

Used internally by BTS7960 and operation code:

| Function | Purpose |
|----------|---------|
| `pwm_init()` | Export both EPWM0 channels, set 1 kHz default |
| `pwm_set_duty_cycle(channel, percent)` | Set PWM duty 0–100% |
| `pwm_enable(channel, bool)` | Enable/disable output |
| `pwm_set_frequency(hz)` | Change frequency (50–20,000 Hz) for both channels |
| `pwm_cleanup()` | Unexport channels |

Use `BTS_RPWM` (0) and `BTS_LPWM` (1) constants to index channels.

## Testing

### Functional Tests

Each subsystem has a dedicated test executable in `app/functiontests/`:

```bash
sudo ./mcp4725_test     # DAC output ramp, millivolt steps, throttle percentage, EEPROM write
sudo ./bts7960_test     # Forward/reverse 4s each at 100%
sudo ./tb6600_test      # Bidirectional step sequences
```

### Full System Integration Test

Interactive menu-driven test that exercises all three subsystems together:

```bash
sudo ./system_integration_test
```

**Menu options:**
1. **launcher** — Set BLDC throttle voltage (1.5–3.9 V); validates input range
2. **angle** — Extend/retract linear actuator in 10 mm increments at 70% duty
3. **rotation** — Rotate launcher clockwise/counter-clockwise in fixed increments
- **q/Q/exit** — Return to main menu or quit program
- **Ctrl+C** — Emergency stop, clean resource shutdown

All resources freed on exit.

## Notes & Best Practices

- **Run with sudo:** All tests and the main application require escalated privileges for GPIO, PWM sysfs, and I2C access.
- **Clean shutdown:** Ctrl+C triggers signal handlers that properly disable motors and free kernel resources (GPIO, PWM channels, I2C file descriptors).
- **Shared PWM frequency:** Both BTS7960 channels (forward/reverse) share EPWM0 timer, so frequency changes affect both simultaneously. Duty cycle is independent.
- **Physics accuracy:** The arc_calc lookup tables assume a ballistic model with gravity = 9.81 m/s². Results depend on accurate calibration of net height and court dimensions.
- **Stepper increments:** TB6600 steps are open-loop; no feedback on actual position. Increments are counted steps only.
- **DAC range:** MCP4725 sets BLDC throttle. Motor stall-spin at ~1.5 V, cutoff behavior above ~3.9 V; safe operating window is ~2.8–3.0 V.
- **Re-compile on file changes:** If you add/remove .c or .h files, re-run `cmake -S . -B build` or just resave `CMakeLists.txt` in VS Code to auto-trigger reconfigure.