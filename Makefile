# Simple Makefile for testing main.c
# Assumes hal library is built separately

CC = gcc
CFLAGS = -Wall -Werror -Wpedantic -Wextra -std=c11 -pthread
LDFLAGS = -pthread -lm

# Source files
#HAL_SOURCES = hal/src/pwm.c
APP_SOURCES = app/src/main.c app/src/fsm.c app/src/calibration.c app/src/arc_calc.c app/src/operation.c app/src/set.c app/src/advanced.c
ALL_SOURCES = $(APP_SOURCES)

# Include directories
HAL_INCLUDES = -Ihal/include
APP_INCLUDES = -Iapp/src/include

# Executable name
TARGET = motor_control

# Default target
all: $(TARGET)

# Build executable
$(TARGET): $(ALL_SOURCES)
	$(CC) $(CFLAGS) $(HAL_INCLUDES) $(APP_INCLUDES) $(ALL_SOURCES) -o $(TARGET) $(LDFLAGS)

# Test target (build and run)
test: $(TARGET)
	./$(TARGET)

# Clean build artifacts
clean:
	rm -f $(TARGET)

# Rebuild everything
rebuild: clean all

.PHONY: all test clean rebuild