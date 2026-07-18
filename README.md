# Gesture Control (webapp3-based)

This repository contains the current, working gesture-control implementation for
the BeagleY-AI stack.

It has two runtime subsystems:

1. Recogniser (`mediapipe_files/` -> `m2demo`)
   - C++ MediaPipe binary built by Bazel.
   - Reads camera frames and sends gesture summaries to UDP `127.0.0.1:12345`.
2. Web app + machine control (`server/` + `app/` + `hal/`)
   - Node server/UI (`server/`).
   - Hardware control via native addons (`node-gyp`) built from this repo's
     C sources in `app/` and `hal/`.

## Important Repo Facts

- `mediapipe/` is external and git-ignored in this repo.
- Build and run natively on the board for real hardware testing.
- The old standalone `volleyball-launcher` daemon path is separate; this repo
  uses in-process native addons for machine control.

## Repository Layout

- `server/`:
  - `server.js` starts the web app.
  - `lib/gesture_server.js` handles socket.io, spawns/stops `m2demo`, receives
    UDP gestures, and calls native addon APIs for launcher control.
  - `binding.gyp` builds `operation_wrapper`, `set_wrapper`, and
    `calibration_wrapper` from `app/` and `hal/`.
- `app/` and `hal/`:
  - C implementation used by native addons (machine control + calibration + set
    operations).
- `mediapipe_files/`:
  - Canonical MediaPipe-side sources (`m2demo.cpp`, `BUILD`, graph, etc.) synced
    into `mediapipe/mediapipe/mediapipe_files/` before Bazel build.

## Local Make Targets (from this folder)

This repo now includes [Makefile](Makefile) at its root (`gesture-control/`):

- `make` or `make all`
  - Runs `make server` then `make m2demo`.
- `make server`
  - `npm install` in `server/`
  - `npm run build` (node-gyp native addons)
- `make m2demo`
  - Syncs `mediapipe_files/*` into external MediaPipe tree
  - Runs Bazel build for `//mediapipe/mediapipe_files:m2demo`
- `make run`
  - Starts `node server.js` with `M2DEMO_BIN` pointed at Bazel output
- `make camera-test`
  - Builds and runs `mediapipe_files/camera_test.cpp`
- `make clean`
  - Removes camera test binary and runs `bazel clean`

## Prerequisites

### Web app + native addons

- Node.js + npm
- Build toolchain for node-gyp (`python3`, `make`, `g++`)
- `libgpiod` development package for addon link step

### Recogniser (Bazel + MediaPipe)

- Bazel/Bazelisk
- C++ toolchain + JDK
- OpenCV development headers
- External `mediapipe/` checkout present at repo root

## Typical Board Workflow

1. Update repo on board (`git pull` on `gesture-wip`) or copy changed files.
2. From repo root (`gesture-control/`):
   - `make server`
   - `make m2demo` (when recogniser sources changed)
   - `make run`

## Gesture Data Path

`m2demo` emits lines like:

`gesture <count> <thumb> <index> <middle> <ring> <pinky> <NAME>`

`server/lib/gesture_server.js` parses these and emits `gesture-update` events to
the browser clients.
