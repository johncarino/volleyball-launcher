# BUILD file for the BeagleY-AI gesture-control binary.
#
# Copy this whole folder into a cloned MediaPipe repo as:
#   mediapipe/mediapipe_files/
# then build (native or cross-compiled for aarch64) with, e.g.:
#
#   bazel build -c opt \
#     --define MEDIAPIPE_DISABLE_GPU=1 \
#     //mediapipe/mediapipe_files:m2demo
#
# For an aarch64 cross-compile add your crosstool/toolchain flags, e.g.:
#   --config=elinux_aarch64
# (see MediaPipe's .bazelrc / the project README for the exact toolchain setup).

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")

licenses(["notice"])

package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "m2demo",
    srcs = [
        "pose_recognition.cpp",
        "pose_recognition.h",
        "m2demo.cpp",
    ],
    data = [
        "pose_tracking_custom.pbtxt",
        "//mediapipe/modules/pose_detection:pose_detection.tflite",
        "//mediapipe/modules/pose_landmark:pose_landmark_full.tflite",
    ],
    deps = [
        "//mediapipe/framework:calculator_framework",
        "//mediapipe/framework/formats:classification_cc_proto",
        "//mediapipe/framework/formats:image_frame",
        "//mediapipe/framework/formats:image_frame_opencv",
        "//mediapipe/framework/formats:landmark_cc_proto",
        "//mediapipe/framework/port:file_helpers",
        "//mediapipe/framework/port:opencv_imgcodecs",
        "//mediapipe/framework/port:opencv_imgproc",
        "//mediapipe/framework/port:opencv_video",
        "//mediapipe/framework/port:parse_text_proto",
        "//mediapipe/framework/port:ret_check",
        "//mediapipe/framework/port:status",
        "//mediapipe/graphs/pose_tracking:pose_tracking_cpu_deps",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/log:initialize",
    ],
)

# NOTE: camera_test.cpp is intentionally NOT a Bazel target. It is a standalone,
# dependency-light OpenCV program meant to be compiled natively on the board:
#   g++ -O2 camera_test.cpp -o camera_test $(pkg-config --cflags --libs opencv4)
# (or via `make camera-test`). It deliberately avoids MediaPipe/Bazel so the
# camera path can be verified without a cross-toolchain or a full MediaPipe build.
