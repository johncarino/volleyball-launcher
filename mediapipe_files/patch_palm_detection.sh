#!/usr/bin/env bash
# patch_palm_detection.sh
#
# Lowers the palm detector's SSD anchor min_scale (and optionally its
# min_score_thresh) inside the external MediaPipe checkout so the hand
# recogniser can pick up hands that appear smaller in frame (i.e. farther
# from the camera).
#
# Background: MediaPipe's palm detector (mediapipe/modules/palm_detection/
# palm_detection_cpu.pbtxt) generates a fixed set of SSD anchor boxes sized
# relative to its 192x192 model input, via SsdAnchorsCalculatorOptions.
# `min_scale` sets the smallest anchor box size (as a fraction of the input);
# a hand that's farther away occupies a smaller fraction of the frame and
# needs a smaller anchor to match against, or it's never detected at all no
# matter how low the confidence threshold is. The stock default is
# 0.1484375. Lowering it (e.g. to 0.04-0.08) lets the detector match much
# smaller/farther hands, at a small cost to detecting very close/large ones
# and a very small extra anchor-count cost.
#
# This script is idempotent: running it again with a different value just
# re-patches the line; running `make m2demo` (which calls this) is always
# safe to repeat.
#
# Usage:
#   ./patch_palm_detection.sh [MEDIAPIPE_ROOT] [MIN_SCALE] [MIN_SCORE_THRESH]
#
# Defaults:
#   MEDIAPIPE_ROOT     $MEDIAPIPE env var, or ../mediapipe relative to repo root
#   MIN_SCALE          0.05   (stock default is 0.1484375)
#   MIN_SCORE_THRESH   unset  (leave the stock 0.5 confidence threshold alone)
#
# Example: also loosen the confidence threshold a bit to help borderline
# far-away detections pass:
#   ./patch_palm_detection.sh "" 0.05 0.35

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
REPO_ROOT="$(dirname -- "$SCRIPT_DIR")"

MEDIAPIPE_ROOT="${1:-${MEDIAPIPE:-$REPO_ROOT/mediapipe}}"
MIN_SCALE="${2:-0.05}"
MIN_SCORE_THRESH="${3:-}"

PALM_PBTXT="$MEDIAPIPE_ROOT/mediapipe/modules/palm_detection/palm_detection_cpu.pbtxt"

if [[ ! -f "$PALM_PBTXT" ]]; then
  echo "error: palm_detection_cpu.pbtxt not found at:" >&2
  echo "  $PALM_PBTXT" >&2
  echo "Pass the MediaPipe checkout root as the first argument, or set \$MEDIAPIPE." >&2
  exit 1
fi

# One-time backup of the pristine file so the patch can be reverted/re-based.
BACKUP="$PALM_PBTXT.orig"
if [[ ! -f "$BACKUP" ]]; then
  cp "$PALM_PBTXT" "$BACKUP"
  echo ">> Backed up pristine file to $BACKUP"
fi

if ! grep -q 'min_scale:' "$PALM_PBTXT"; then
  echo "error: no 'min_scale:' field found in $PALM_PBTXT (unexpected MediaPipe version?)" >&2
  exit 1
fi

sed -i -E "s/(min_scale:[[:space:]]*)[0-9.]+/\1${MIN_SCALE}/" "$PALM_PBTXT"
echo ">> Set min_scale: $MIN_SCALE in $PALM_PBTXT"

if [[ -n "$MIN_SCORE_THRESH" ]]; then
  if ! grep -q 'min_score_thresh:' "$PALM_PBTXT"; then
    echo "error: no 'min_score_thresh:' field found in $PALM_PBTXT" >&2
    exit 1
  fi
  sed -i -E "s/(min_score_thresh:[[:space:]]*)[0-9.]+/\1${MIN_SCORE_THRESH}/" "$PALM_PBTXT"
  echo ">> Set min_score_thresh: $MIN_SCORE_THRESH in $PALM_PBTXT"
fi

echo ">> Done. Rebuild with 'make m2demo' (or bazel build directly) to pick this up."
