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

## Adding a new HAL module

`server/binding.gyp` does **not** glob `hal/src/*.c` — each addon target lists its
`.c`/`.cpp` sources explicitly. Adding a new HAL module (e.g. a new sensor driver
under `hal/src/`) requires **two steps**:

1. Add the new `.c` file to the `sources` array of the relevant target in
   [server/binding.gyp](server/binding.gyp) (usually `operation_wrapper`, since
   that's the target linking the HAL). Add any new `libraries` flag too if the
   module needs one (e.g. a new `-l...`).
2. Run `make server` to rebuild the native addons (`npm run build` ->
   `node-gyp rebuild`).

Running `make server` alone, **without** editing `binding.gyp` first, will not
pick up the new file — node-gyp only compiles what's listed.

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

## Syncing to the Board

The board (BeagleY-AI, `john@192.168.7.2`) has its own clone of this repo,
currently checked out at `~/Downloads/volleyball-system/gesture-control`. Two
ways to get local changes onto it:

- **`git pull`** (preferred once changes are committed/pushed) — run on the
  board inside that directory, on the `gesture-wip` branch.
- **`rsync`** (for iterating before a commit, or copying uncommitted changes):

  ```bash
  rsync -avz --delete \
      --exclude '.git' \
      --exclude 'mediapipe/' \
      /home/john/Downloads/volleyball-launcher/ \
      john@192.168.7.2:/home/john/Downloads/volleyball-system/gesture-control/
  ```

  `--exclude 'mediapipe/'` is required — the board's external MediaPipe
  checkout lives at that path and must never be deleted/overwritten by
  `--delete`. Drop `--delete` if you'd rather leave stale files on the board
  in place.

  To copy a single changed file instead:

  ```bash
  scp server/lib/gesture_server.js \
      john@192.168.7.2:/home/john/Downloads/volleyball-system/gesture-control/server/lib/gesture_server.js
  ```

## Typical Board Workflow

1. Update repo on board (`git pull` on `gesture-wip`) or copy changed files
   (see [Syncing to the Board](#syncing-to-the-board)).
2. From repo root (`gesture-control/`):
   - `make server`
   - `make m2demo` (when recogniser sources changed)
   - `make run`

## Gesture Data Path

`m2demo` emits lines like:

`gesture <count> <thumb> <index> <middle> <ring> <pinky> <NAME>`

`server/lib/gesture_server.js` parses these and emits `gesture-update` events to
the browser clients.

## Known Gotcha: `m2demo` Resource Loading

Bazel-built binaries resolve MediaPipe resource paths (e.g.
`mediapipe/modules/palm_detection/palm_detection_full.tflite`) relative to the
process's working directory, assuming they're launched the way `bazel run`
launches them — from inside the target's generated runfiles tree. Since
`gesture_server.js` spawns `m2demo` directly, it sets the child process's
`cwd` to `<M2DEMO_BIN>.runfiles/_main` (the runfiles workspace directory name
under Bazel's bzlmod, confirmed via `ls .runfiles/`) so those relative lookups
succeed. If model loading fails with `Can't find file: mediapipe/modules/...`,
check that this runfiles directory exists next to the built binary, or
override the path with the `M2DEMO_RUNFILES_DIR` environment variable if your
Bazel setup names it differently.
