# Gesture-control local entry point (repo-root Makefile)
#
# This Makefile is scoped to the gesture-control repository itself.
# It builds/runs the current webapp3-style stack:
#   - Node web app + native C addons (node-gyp)
#   - MediaPipe recogniser (m2demo) from mediapipe_files/
#
# NOTE:
# - `mediapipe/` is an external checkout and must already exist locally.
# - Build on the board for runtime testing so native addons match board arch.

ROOT      := $(CURDIR)
MEDIAPIPE := $(ROOT)/mediapipe
SERVER    := $(ROOT)/server

# Optional Bazel flags (left overridable for advanced use).
BAZEL_CONFIG ?=

# Where Bazel drops the recogniser.
M2DEMO_BIN := $(MEDIAPIPE)/bazel-bin/mediapipe/mediapipe_files/m2demo

# Standalone camera test source/binary.
CAMERA_TEST_SRC := $(ROOT)/mediapipe_files/camera_test.cpp
CAMERA_TEST_BIN := $(ROOT)/mediapipe_files/camera_test

.PHONY: all server m2demo camera-test run clean help

all: server m2demo

## Install npm deps + build native C addons (node-gyp).
server:
	@echo ">> Installing web server dependencies (npm)..."
	cd $(SERVER) && npm install
	@echo ">> Building native C control addons (node-gyp)..."
	cd $(SERVER) && npm run build

## Build m2demo via Bazel, syncing canonical sources first.
m2demo:
	@echo ">> Syncing canonical mediapipe_files into the Bazel tree..."
	cp $(ROOT)/mediapipe_files/* $(MEDIAPIPE)/mediapipe/mediapipe_files/
	@echo ">> Building m2demo (Bazel)..."
	cd $(MEDIAPIPE) && bazel build -c opt $(BAZEL_CONFIG) \
		--define MEDIAPIPE_DISABLE_GPU=1 \
		//mediapipe/mediapipe_files:m2demo

## Build and run the standalone camera test.
camera-test:
	@echo ">> Building camera_test (g++, native)..."
	g++ -O2 -std=c++17 $(CAMERA_TEST_SRC) -o $(CAMERA_TEST_BIN) \
		$(shell pkg-config --cflags --libs opencv4)
	@echo ">> Running camera_test..."
	$(CAMERA_TEST_BIN)

## Run web app; recogniser is spawned by server when Start is pressed.
run:
	@echo ">> Starting web server on http://localhost:8088 ..."
	cd $(SERVER) && M2DEMO_BIN=$(M2DEMO_BIN) node server.js

clean:
	@echo ">> Cleaning build artefacts..."
	-rm -f $(CAMERA_TEST_BIN)
	-cd $(MEDIAPIPE) && bazel clean

help:
	@grep -E '^[a-zA-Z_-]+:' $(firstword $(MAKEFILE_LIST)) | grep -v '\.PHONY'
