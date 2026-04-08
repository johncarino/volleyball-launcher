# Simple Makefile for building local test binaries without CMake

CC = gcc
CFLAGS = -Wall -Werror -Wpedantic -Wextra -std=c11 -pthread -D_DEFAULT_SOURCE
LDFLAGS = -pthread -lm -lgpiod

# Source files
HAL_SOURCES = hal/src/bts7960.c hal/src/mcp4725.c hal/src/pwm.c hal/src/tb6600.c
APP_SHARED_SOURCES = app/src/advanced.c app/src/arc_calc.c app/src/calibration.c app/src/fsm.c app/src/operation.c app/src/set.c
FSM_TEST_SOURCES = app/functiontests/fsm_test.c $(APP_SHARED_SOURCES) $(HAL_SOURCES)

# Include directories
HAL_INCLUDES = -Ihal/include
APP_INCLUDES = -Iapp/src/include

# Executable name
TARGET = motor_control
FSM_TEST_TARGET = fsm_test

# Default target
all: $(FSM_TEST_TARGET)

# Build FSM function test executable
$(FSM_TEST_TARGET): $(FSM_TEST_SOURCES)
	$(CC) $(CFLAGS) $(HAL_INCLUDES) $(APP_INCLUDES) $(FSM_TEST_SOURCES) -o $(FSM_TEST_TARGET) $(LDFLAGS)

# Build and run FSM test
test-fsm: $(FSM_TEST_TARGET)
	./$(FSM_TEST_TARGET)

# Clean build artifacts
clean:
	rm -f $(TARGET) $(FSM_TEST_TARGET)

# Rebuild everything
rebuild: clean all

.PHONY: all test-fsm clean rebuild