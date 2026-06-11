# BeagleY-AI Gesture Control

A barebones, portable hand-gesture-recognition project for the **BeagleY-AI**,
built to be dropped into a larger capstone codebase.

It has two halves:

1. **Recogniser (`mediapipe_files/`)** — a small C++ program (`m2demo`) built on
   top of Google **MediaPipe** hand tracking. It reads the camera, classifies
   which fingers are up + a named gesture, and **pushes** a one-line summary over
   UDP to `127.0.0.1:12345`.
2. **Web app (`server/`)** — a Node.js (`socket.io`) server that lets a browser
   **Start/Stop** the recogniser and shows the live gesture readout. It binds the
   UDP port, relays each summary to the browser, and spawns/kills `m2demo`.

```
Camera ──OpenCV──► m2demo ──UDP 12345──► Node (gesture_server.js) ──socket.io──► Browser
                     ▲                          │
                     └───── spawn / kill ◄───────┘  (Start / Stop buttons)
```

The recogniser and the web app are decoupled by the UDP protocol, so you can test
the entire web stack **without a camera** using the included fake source.

---

## Repository layout

```
mediapipe_files/                 # Copy into a cloned MediaPipe repo to build
  BUILD                          #   Bazel target //mediapipe/mediapipe_files:m2demo
  hand_tracking_custom.pbtxt     #   MediaPipe graph (num_hands=1, CPU)
  hand_recognition.h / .cpp      #   21 landmarks -> fingers-up + named gesture
  m2demo.cpp                     #   Camera -> graph -> UDP push (headless)
server/
  server.js                      # HTTP static server (port 8088)
  lib/gesture_server.js          # socket.io + UDP receiver + spawn/kill m2demo
  public/index.html              # Live gesture UI
  public/javascripts/gesture_ui.js
  fake_gesture_udp.c             # Hardware-free fake gesture source (pushes UDP)
documents/                       # (old project diagrams/poster — safe to delete)
```

## UDP wire protocol

One ASCII line per datagram, sent to `127.0.0.1:12345`:

```
gesture <count> <thumb> <index> <middle> <ring> <pinky> <NAME>
e.g.    gesture 2 0 1 1 0 0 PEACE
```

Each finger is `1` (up) or `0` (down). `NAME` is one of
`FIST, OPEN_PALM, POINT, PEACE, THUMBS_UP, CALL_ME, FOUR, ROCK, GUN, HORNS,
UNKNOWN, NONE`.

---

## Quick start (no hardware needed)

```bash
cd server
npm install                 # installs socket.io + mime
node server.js              # serves http://<board-ip>:8088

# in a second terminal, fake a camera:
gcc -O2 -Wall -Wextra -o fake_gesture_udp fake_gesture_udp.c
./fake_gesture_udp 500      # push a new gesture every 500 ms
```

Open `http://<board-ip>:8088` and watch the live readout cycle through gestures.
(The fake source pushes directly, so you do not even need to press **Start** to
see data; Start/Stop control the real `m2demo` binary.)

---

## Building the real recogniser (`m2demo`)

> Phase 0 is board-specific and is the highest-risk step. The camera
> (Freenove FNK0056 = IMX219, RPi Cam v2 clone) attaches to the **CSI** port and
> needs a working device-tree overlay so it enumerates as `/dev/videoN`. A USB
> webcam works too and is the easiest fallback while bringing up CSI.

1. **Camera bring-up.** Confirm the camera works first:
   ```bash
   v4l2-ctl --list-devices
   # quick capture sanity check:
   ffmpeg -f v4l2 -i /dev/video0 -frames 1 test.jpg
   ```
   For the CSI IMX219 you may need a board overlay and a `media-ctl` pipeline
   setup; a USB webcam usually appears as `/dev/video0` with no extra config.

2. **Get MediaPipe and drop in these files.**
   ```bash
   git clone https://github.com/google-ai-edge/mediapipe.git
   cp -r mediapipe_files mediapipe/mediapipe/mediapipe_files
   ```

3. **Build (CPU only).** Native build on the board, or cross-compile for aarch64:
   ```bash
   cd mediapipe
   bazel build -c opt \
     --define MEDIAPIPE_DISABLE_GPU=1 \
     //mediapipe/mediapipe_files:m2demo
   # cross-compile: add your aarch64 toolchain, e.g. --config=elinux_aarch64
   ```
   The resulting binary is `bazel-bin/mediapipe/mediapipe_files/m2demo`.

4. **Run it manually (optional):**
   ```bash
   ./bazel-bin/mediapipe/mediapipe_files/m2demo \
     --calculator_graph_config_file=mediapipe/mediapipe_files/hand_tracking_custom.pbtxt \
     --camera_index=0 --udp_port=12345
   ```

## Wiring m2demo to the web app

`gesture_server.js` spawns `m2demo` when you press **Start**. Tell it where the
binary and graph live via environment variables before launching Node:

```bash
export M2DEMO_BIN=/path/to/mediapipe/bazel-bin/mediapipe/mediapipe_files/m2demo
export GESTURE_GRAPH=/path/to/mediapipe/mediapipe/mediapipe_files/hand_tracking_custom.pbtxt
export GESTURE_CAMERA=0            # /dev/videoN index
node server.js
```

| Variable          | Default                                   | Meaning                         |
|-------------------|-------------------------------------------|---------------------------------|
| `M2DEMO_BIN`      | `m2demo` (on PATH)                         | Recogniser binary               |
| `GESTURE_GRAPH`   | `../../mediapipe_files/hand_tracking_custom.pbtxt` | Graph config path      |
| `GESTURE_CAMERA`  | `0`                                       | OpenCV camera index             |
| `GESTURE_UDP_PORT`| `12345`                                   | UDP port (must match m2demo)    |
| `GESTURE_UDP_HOST`| `127.0.0.1`                               | UDP host                        |

## Importing into your capstone

- Reuse the **UDP protocol** as the integration boundary: anything that can parse
  the `gesture ...` line can consume gestures, web app or not.
- `hand_recognition.{h,cpp}` is self-contained — lift it into another MediaPipe
  binary to get fingers-up + gesture names elsewhere.
- The web layer (`server/`) is independent of the recogniser and can be embedded
  as-is or replaced.

## Notes / tuning

- `num_hands` defaults to **1** in `hand_tracking_custom.pbtxt` for lower CPU on
  the BeagleY-AI; bump it to 2 for two-handed gestures.
- Sends are throttled to ~10 Hz (`--send_interval_ms`, default 100). MediaPipe
  inference dominates CPU; the UDP/websocket transport cost is negligible.
- Finger detection assumes a roughly upright hand and is intentionally simple —
  a solid base to extend in `hand_recognition.cpp`.
