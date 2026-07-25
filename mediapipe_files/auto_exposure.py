#!/usr/bin/env python3
"""Software auto-exposure prototype for the BeagleY-AI IMX219 (no ISP).

The BeagleY-AI has no working libcamera/ISP path, so nothing runs the usual
auto-exposure loop. This script *is* that loop, done in software:

    1. Capture frames via the same raw-Bayer GStreamer pipeline camera_test uses
       (v4l2src -> bayer2rgb -> BGR appsink), consumed by OpenCV.
    2. Measure mean luma of each frame.
    3. Nudge the sensor's `exposure` (then `analogue_gain` when exposure is
       maxed) toward a target brightness.
    4. Write the new values back with `v4l2-ctl` on the sensor sub-device.

IMPORTANT: the camera is single-owner. Run this only while m2demo is STOPPED
(the Start button not pressed) or it will fight for /dev/video-imx219-cam0.
Use it to find good --target / gain limits, then port the same control law into
m2demo.cpp for production (see the note at the bottom of this file).

Examples:
    ./auto_exposure.py                       # converge, print, exit
    ./auto_exposure.py --target 120 --save exp.jpg
    ./auto_exposure.py --subdev /dev/v4l-subdev2   # skip auto-detect
"""

import argparse
import re
import subprocess
import sys
import time

import cv2


# --- Sensor control access via v4l2-ctl ------------------------------------

def list_ctrls(subdev):
    """Return {name: {min, max, value}} for the sub-device's integer controls."""
    out = subprocess.run(
        ["v4l2-ctl", "-d", subdev, "--list-ctrls"],
        capture_output=True, text=True,
    ).stdout
    ctrls = {}
    # e.g. "exposure 0x00980911 (int) : min=4 max=1759 step=1 default=1600 value=1600"
    pat = re.compile(
        r"^\s*(\w+)\s+0x[0-9a-f]+\s+\(int\)\s*:\s*"
        r"min=(-?\d+)\s+max=(-?\d+).*?value=(-?\d+)", re.I)
    for line in out.splitlines():
        m = pat.match(line)
        if m:
            name, lo, hi, val = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))
            ctrls[name] = {"min": lo, "max": hi, "value": val}
    return ctrls


def find_sensor_subdev():
    """Auto-detect the sub-device that exposes an `exposure` control."""
    # v4l-subdev nodes are where sensor controls live (not the /dev/videoN node).
    for n in range(16):
        dev = f"/dev/v4l-subdev{n}"
        try:
            if "exposure" in list_ctrls(dev):
                return dev
        except FileNotFoundError:
            break  # v4l2-ctl not installed
        except Exception:
            continue
    return None


def get_ctrl(subdev, name):
    """Read back a single control's current value (None if absent)."""
    return list_ctrls(subdev).get(name, {}).get("value")


def set_ctrl(subdev, name, value):
    """Set a control, then read it back and warn if the sensor ignored the write."""
    subprocess.run(
        ["v4l2-ctl", "-d", subdev, "--set-ctrl", f"{name}={int(value)}"],
        check=False,
    )
    actual = get_ctrl(subdev, name)
    if actual is not None and actual != int(value):
        print(f"  ! {name} write ignored: asked {int(value)}, sensor reports {actual}")
    return actual


# --- GStreamer capture pipeline (mirrors camera_test.cpp) -------------------

def build_pipeline(device, bayer, sw, sh, ow, oh):
    return (
        f"v4l2src device={device} ! "
        f"video/x-bayer,format={bayer},width={sw},height={sh} ! "
        f"bayer2rgb ! videoconvert ! videoscale ! "
        f"video/x-raw,format=BGR,width={ow},height={oh} ! appsink"
    )


def mean_luma(bgr):
    """Average perceived brightness (0..255) of a BGR frame."""
    gray = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)
    return float(gray.mean())


def white_balance(bgr):
    """Gray-world white balance.

    Raw IMX219 frames have a heavy green cast because the sensor has twice as
    many green photosites (and higher green sensitivity) and there is no ISP
    doing AWB. Equalise the per-channel means so a neutral scene reads neutral.
    """
    b, g, r = cv2.split(bgr.astype("float32"))
    mb, mg, mr = b.mean(), g.mean(), r.mean()
    mgray = (mb + mg + mr) / 3.0
    b *= mgray / max(mb, 1.0)
    g *= mgray / max(mg, 1.0)
    r *= mgray / max(mr, 1.0)
    return cv2.merge([b, g, r]).clip(0, 255).astype("uint8")


# --- Auto-exposure control loop --------------------------------------------

def main():
    ap = argparse.ArgumentParser(description="Software AE prototype for IMX219.")
    ap.add_argument("--subdev", default=None,
                    help="sensor sub-device (default: auto-detect)")
    ap.add_argument("--device", default="/dev/video-imx219-cam0")
    ap.add_argument("--bayer", default="rggb")
    ap.add_argument("--sensor-width", type=int, default=1920)
    ap.add_argument("--sensor-height", type=int, default=1080)
    ap.add_argument("--width", type=int, default=640)
    ap.add_argument("--height", type=int, default=480)
    ap.add_argument("--target", type=float, default=110.0,
                    help="desired mean luma, 0..255 (default 110)")
    ap.add_argument("--deadband", type=float, default=6.0,
                    help="stop adjusting once within this of the target")
    ap.add_argument("--gain-factor", type=float, default=0.6,
                    help="fraction of the ideal correction applied per step "
                         "(<1 damps oscillation)")
    ap.add_argument("--max-gain", type=int, default=None,
                    help="cap analogue_gain (default: driver max)")
    ap.add_argument("--vblank", type=int, default=None,
                    help="set vertical_blanking at startup to raise the exposure "
                         "ceiling. Max exposure = frame height + vblank, so a "
                         "larger value allows a longer exposure (but a lower frame "
                         "rate). The IMX219 default (~683) caps exposure near 1759 "
                         "lines, too short to expose a normally-lit room.")
    ap.add_argument("--digital-gain", type=int, default=256,
                    help="digital_gain to set at startup (256=1.0x .. 4095=16x)")
    ap.add_argument("--iterations", type=int, default=40,
                    help="max control steps before giving up / exiting")
    ap.add_argument("--settle-frames", type=int, default=3,
                    help="frames to discard after each control change so the "
                         "new exposure takes effect")
    ap.add_argument("--save", default=None, help="write the final frame here")
    ap.add_argument("--continuous", action="store_true",
                    help="keep running instead of exiting once converged")
    ap.add_argument("--no-flip", action="store_true",
                    help="don't rotate 180 (the sensor is mounted upside-down)")
    ap.add_argument("--no-wb", action="store_true",
                    help="don't apply gray-world white balance (raw IMX219 is green-cast)")
    args = ap.parse_args()

    subdev = args.subdev or find_sensor_subdev()
    if not subdev:
        sys.exit("Could not find the sensor sub-device. Pass --subdev "
                 "(see: v4l2-ctl --list-devices / media-ctl -p).")
    print(f"Using sensor sub-device: {subdev}")

    # Establish a known starting state BEFORE streaming. The sensor applies the
    # control handler's current values at stream-start, and the exposure ceiling
    # is (frame height + vertical_blanking) -- so raising vblank here is what lets
    # exposure go long enough to properly expose a normally-lit room.
    if args.vblank is not None:
        set_ctrl(subdev, "vertical_blanking", args.vblank)
    set_ctrl(subdev, "digital_gain", args.digital_gain)

    ctrls = list_ctrls(subdev)
    if "exposure" not in ctrls:
        sys.exit(f"{subdev} has no 'exposure' control; wrong sub-device?")
    exp_lo, exp_hi = ctrls["exposure"]["min"], ctrls["exposure"]["max"]
    exposure = ctrls["exposure"]["value"]

    have_gain = "analogue_gain" in ctrls
    gain_lo = ctrls["analogue_gain"]["min"] if have_gain else 0
    gain_hi = ctrls["analogue_gain"]["max"] if have_gain else 0
    if args.max_gain is not None:
        gain_hi = min(gain_hi, args.max_gain)
    gain = ctrls["analogue_gain"]["value"] if have_gain else 0

    pipeline = build_pipeline(args.device, args.bayer, args.sensor_width,
                              args.sensor_height, args.width, args.height)
    print(f"Opening camera:\n  {pipeline}")
    cap = cv2.VideoCapture(pipeline, cv2.CAP_GSTREAMER)
    if not cap.isOpened():
        sys.exit("Could not open the camera (is m2demo stopped? is OpenCV "
                 "built with GStreamer?).")

    def grab():
        for _ in range(args.settle_frames):
            cap.read()          # discard frames captured under the old setting
        ok, frame = cap.read()
        if not (ok and frame is not None):
            return None
        if not args.no_flip:
            frame = cv2.flip(frame, -1)    # sensor mounted upside-down -> rotate 180
        if not args.no_wb:
            frame = white_balance(frame)   # correct the raw green cast
        return frame

    try:
        step = 0
        while True:
            frame = grab()
            if frame is None:
                print("No frame; retrying...")
                time.sleep(0.05)
                continue

            luma = mean_luma(frame)
            err = args.target - luma
            print(f"[{step:02d}] luma={luma:6.1f} target={args.target:.0f} "
                  f"exposure={exposure} gain={gain}")

            if abs(err) <= args.deadband:
                if not args.continuous:
                    print("Converged.")
                    break
                time.sleep(0.2)
                continue

            # Multiplicative control: brightness scales ~linearly with both
            # exposure and gain, so aim for exposure *= target/luma, damped.
            ratio = args.target / max(luma, 1.0)
            ratio = 1.0 + (ratio - 1.0) * args.gain_factor

            if err > 0:  # too dark: raise exposure first, then gain
                new_exp = min(int(exposure * ratio), exp_hi)
                if new_exp != exposure:
                    exposure = new_exp
                    set_ctrl(subdev, "exposure", exposure)
                elif have_gain and gain < gain_hi:  # exposure maxed -> add gain
                    gain = min(int(max(gain, 1) * ratio), gain_hi)
                    set_ctrl(subdev, "analogue_gain", gain)
                else:
                    print("Can't get brighter (exposure & gain maxed).")
                    if not args.continuous:
                        break
            else:        # too bright: drop gain first, then exposure
                if have_gain and gain > gain_lo:
                    gain = max(int(gain * ratio), gain_lo)
                    set_ctrl(subdev, "analogue_gain", gain)
                else:
                    new_exp = max(int(exposure * ratio), exp_lo)
                    if new_exp != exposure:
                        exposure = new_exp
                        set_ctrl(subdev, "exposure", exposure)
                    else:
                        print("Can't get darker (exposure & gain at min).")
                        if not args.continuous:
                            break

            step += 1
            if step >= args.iterations:
                print("Reached iteration limit.")
                break

        if args.save:
            frame = grab()
            if frame is not None and cv2.imwrite(args.save, frame):
                print(f"Saved final frame to {args.save}")
        print(f"Final: exposure={exposure} analogue_gain={gain}")
    finally:
        cap.release()


if __name__ == "__main__":
    main()

# --- Porting into m2demo.cpp (production) ----------------------------------
# The camera is owned by m2demo during operation, so the real AE loop belongs
# inside its capture loop. The C++ version of this control law is ~30 lines:
#   1. Once at startup, open the sensor sub-device and read the exposure/gain
#      ranges (VIDIOC_QUERYCTRL) — or just hardcode the values you settle on.
#   2. Every N captured frames, compute mean luma with cv::mean(gray).
#   3. Apply the same multiplicative, damped correction above.
#   4. Write exposure/gain back with VIDIOC_S_CTRL on the sub-device fd
#      (or system("v4l2-ctl ...") for a quick first cut).
# Run it every ~10 frames, not every frame, so it stays cheap and stable.
