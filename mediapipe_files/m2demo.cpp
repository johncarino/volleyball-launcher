// m2demo.cpp
//
// Headless MediaPipe hand-gesture recogniser for the BeagleY-AI.
//
// Pipeline:
//   CSI / USB camera --(OpenCV)--> input_video --[hand_tracking_custom.pbtxt]-->
//   landmarks --(hand_recognition)--> gesture summary --(UDP)--> Node backend.
//
// The Node backend (server/lib/gesture_server.js) binds UDP 127.0.0.1:12345 and
// relays each summary to the browser over socket.io. This program is spawned and
// killed by that backend in response to the web UI's Start/Stop buttons.
//
// Based on MediaPipe's example
//   mediapipe/examples/desktop/demo_run_graph_main.cc
// with the GUI display replaced by a throttled UDP push and a "no hand" timeout.

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/absl_log.h"
#include "mediapipe/framework/calculator_framework.h"
#include "mediapipe/framework/formats/image_frame.h"
#include "mediapipe/framework/formats/image_frame_opencv.h"
#include "mediapipe/framework/formats/landmark.pb.h"
#include "mediapipe/framework/port/file_helpers.h"
#include "mediapipe/framework/port/opencv_imgproc_inc.h"
#include "mediapipe/framework/port/opencv_video_inc.h"
#include "mediapipe/framework/port/parse_text_proto.h"
#include "mediapipe/framework/port/ret_check.h"
#include "mediapipe/framework/port/status.h"
#include "mediapipe/mediapipe_files/hand_recognition.h"

namespace {
constexpr char kInputStream[] = "input_video";
constexpr char kOutputStream[] = "output_video";
constexpr char kLandmarksStream[] = "landmarks";
}  // namespace

ABSL_FLAG(std::string, calculator_graph_config_file, "",
          "Path to the text CalculatorGraphConfig proto "
          "(hand_tracking_custom.pbtxt).");
ABSL_FLAG(std::string, input_video_path, "",
          "Optional video file to use instead of a live camera.");
ABSL_FLAG(bool, use_gstreamer, true,
          "Use the V4L2/GStreamer Bayer pipeline (required for the CSI IMX219 on "
          "the BeagleY-AI). Set false to use a USB camera via OpenCV's plain V4L2 "
          "backend and --camera_index.");
ABSL_FLAG(std::string, camera_device, "/dev/video-imx219-cam0",
          "V4L2 device node for the CSI IMX219 (used when --use_gstreamer=true).");
ABSL_FLAG(std::string, bayer_format, "rggb",
          "Bayer phase (rggb/grbg/gbrg/bggr) — try others if colours look off.");
ABSL_FLAG(int, sensor_width, 1920, "Native sensor capture width (Bayer).");
ABSL_FLAG(int, sensor_height, 1080, "Native sensor capture height (Bayer).");
ABSL_FLAG(bool, rotate180, true,
          "Rotate frames 180° (the camera is mounted upside-down). Required for "
          "the upright-hand assumption in the gesture logic.");
ABSL_FLAG(bool, white_balance, true,
          "Apply gray-world white balance to counter the green cast of software "
          "debayering. On by default so colours match a normal camera.");
ABSL_FLAG(int, camera_index, 0,
          "OpenCV camera index, i.e. N for /dev/videoN. Used only when "
          "--use_gstreamer=false (USB camera).");
ABSL_FLAG(int, capture_width, 640, "Output (downscaled) frame width.");
ABSL_FLAG(int, capture_height, 480, "Output (downscaled) frame height.");
ABSL_FLAG(std::string, udp_host, "127.0.0.1",
          "Host to send gesture summaries to.");
ABSL_FLAG(int, udp_port, 12345, "UDP port to send gesture summaries to.");
ABSL_FLAG(int, send_interval_ms, 100,
          "Minimum interval between UDP sends (throttle, default ~10 Hz).");
ABSL_FLAG(std::string, stream_host, "",
          "If set, MJPEG/RTP-stream the annotated frames to this host (e.g. your "
          "laptop's IP) over UDP for a low-latency live preview. Empty (default) "
          "disables streaming so the board pays no extra cost.");
ABSL_FLAG(int, stream_port, 5000,
          "UDP port for the --stream_host live preview stream.");

ABSL_FLAG(bool, auto_exposure, true,
          "Run the software auto-exposure loop for the ISP-less CSI IMX219 (no "
          "effect for USB cameras or --input_video_path). Without it the raw "
          "sensor is badly underexposed indoors.");
ABSL_FLAG(std::string, sensor_subdev, "",
          "Sensor v4l-subdev node for AE (default: auto-detect the one exposing "
          "an 'exposure' control).");
ABSL_FLAG(int, ae_target, 110, "Target mean luma for AE, 0..255.");
ABSL_FLAG(int, ae_vblank, 6000,
          "vertical_blanking to set at startup. Raises the exposure ceiling "
          "(max exposure = frame height + vblank) so the loop can expose a "
          "normally-lit room; higher = brighter-capable but lower frame rate. "
          "0 leaves the sensor default, which caps exposure too short.");
ABSL_FLAG(int, ae_digital_gain, 256,
          "digital_gain to set at startup (256=1.0x .. 4095=16x).");
ABSL_FLAG(int, ae_interval, 10, "Run one AE step every N captured frames.");
ABSL_FLAG(int, ae_max_gain, 0,
          "Cap analogue_gain (0 = use the driver maximum).");

namespace {

// Build the camera capture GStreamer pipeline for the BeagleY-AI CSI IMX219.
// The board has no working libcamera/ISP path, so we capture raw RGGB Bayer from
// the TI CSI node and debayer + scale to BGR in software
// (v4l2src -> bayer2rgb -> videoconvert/videoscale -> BGR appsink).
std::string BuildGstPipeline(const std::string& device,
                             const std::string& bayer_format, int sensor_width,
                             int sensor_height, int out_width, int out_height) {
  return "v4l2src device=" + device + " ! " +
         "video/x-bayer,format=" + bayer_format +
         ",width=" + std::to_string(sensor_width) +
         ",height=" + std::to_string(sensor_height) + " ! " +
         "bayer2rgb ! videoconvert ! videoscale ! " +
         "video/x-raw,format=BGR,width=" + std::to_string(out_width) +
         ",height=" + std::to_string(out_height) + " ! appsink";
}

// Gray-world white balance: scale each BGR channel so their means match the
// global mean. Cheap stand-in for the auto-white-balance the (absent) ISP would
// normally do; counteracts the green cast of naive software debayering.
void ApplyGrayWorld(cv::Mat& bgr) {
  const cv::Scalar means = cv::mean(bgr);  // [B, G, R]
  const double avg = (means[0] + means[1] + means[2]) / 3.0;
  if (means[0] <= 0 || means[1] <= 0 || means[2] <= 0) return;
  std::vector<cv::Mat> ch;
  cv::split(bgr, ch);
  ch[0].convertTo(ch[0], -1, avg / means[0]);
  ch[1].convertTo(ch[1], -1, avg / means[1]);
  ch[2].convertTo(ch[2], -1, avg / means[2]);
  cv::merge(ch, bgr);
}

// --- Software auto-exposure for the ISP-less CSI IMX219 -------------------
// The BeagleY-AI has no ISP, so nothing runs the usual auto-exposure loop. We
// do it in software (ported from mediapipe_files/auto_exposure.py): find the
// sensor sub-device, raise its exposure ceiling via vertical_blanking, then
// every few frames nudge exposure (then analogue gain) toward a target mean
// luma with v4l2-ctl. Without this the raw sensor underexposes badly indoors.

std::string RunAndCapture(const std::string& cmd) {
  std::string out;
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) return out;
  char buf[256];
  while (fgets(buf, sizeof(buf), pipe) != nullptr) out += buf;
  pclose(pipe);
  return out;
}

// Read a sensor control's min/max/value from `v4l2-ctl --list-ctrls`.
bool QueryCtrl(const std::string& subdev, const std::string& name, int* lo,
               int* hi, int* val) {
  const std::string out =
      RunAndCapture("v4l2-ctl -d " + subdev + " --list-ctrls 2>/dev/null");
  std::istringstream iss(out);
  std::string line;
  while (std::getline(iss, line)) {
    if (line.find(name) == std::string::npos) continue;
    const size_t pmin = line.find("min=");
    const size_t pmax = line.find("max=");
    const size_t pval = line.find("value=");
    if (pmin == std::string::npos || pmax == std::string::npos ||
        pval == std::string::npos) {
      continue;
    }
    if (lo) *lo = std::atoi(line.c_str() + pmin + 4);
    if (hi) *hi = std::atoi(line.c_str() + pmax + 4);
    if (val) *val = std::atoi(line.c_str() + pval + 6);
    return true;
  }
  return false;
}

void SetCtrl(const std::string& subdev, const std::string& name, int value) {
  const std::string cmd = "v4l2-ctl -d " + subdev + " --set-ctrl " + name + "=" +
                          std::to_string(value) + " >/dev/null 2>&1";
  const int rc = std::system(cmd.c_str());
  (void)rc;  // best-effort; QueryCtrl read-backs will reveal ignored writes
}

// Auto-detect the sub-device that exposes an `exposure` control (the sensor).
std::string FindSensorSubdev() {
  for (int n = 0; n < 16; ++n) {
    const std::string dev = "/dev/v4l-subdev" + std::to_string(n);
    if (access(dev.c_str(), F_OK) != 0) continue;
    if (QueryCtrl(dev, "exposure", nullptr, nullptr, nullptr)) return dev;
  }
  return "";
}

// Damped multiplicative AE: exposure is the primary lever, analogue gain the
// secondary one. Mirrors the control law in auto_exposure.py.
class SoftwareAe {
 public:
  bool Init(const std::string& subdev, int vblank, int digital_gain, int target,
            int max_gain) {
    subdev_ = subdev.empty() ? FindSensorSubdev() : subdev;
    if (subdev_.empty()) return false;
    // Raise the exposure ceiling (max exposure == frame height + vblank) and set
    // a known digital gain BEFORE reading ranges, so exp_hi_ reflects it.
    if (vblank > 0) SetCtrl(subdev_, "vertical_blanking", vblank);
    SetCtrl(subdev_, "digital_gain", digital_gain);
    if (!QueryCtrl(subdev_, "exposure", &exp_lo_, &exp_hi_, &exposure_)) {
      return false;
    }
    have_gain_ =
        QueryCtrl(subdev_, "analogue_gain", &gain_lo_, &gain_hi_, &gain_);
    if (have_gain_ && max_gain > 0) gain_hi_ = std::min(gain_hi_, max_gain);
    target_ = target;
    return true;
  }

  const std::string& subdev() const { return subdev_; }

  // Nudge exposure (then gain) toward the target luma. Call every N frames.
  void Update(const cv::Mat& bgr) {
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    const double luma = cv::mean(gray)[0];
    const double err = target_ - luma;
    if (std::fabs(err) <= kDeadband) return;

    double ratio = target_ / std::max(luma, 1.0);
    ratio = 1.0 + (ratio - 1.0) * kGainFactor;  // damp to avoid oscillation

    if (err > 0.0) {  // too dark: raise exposure first, then gain
      const int new_exp =
          std::min(static_cast<int>(exposure_ * ratio), exp_hi_);
      if (new_exp != exposure_) {
        exposure_ = new_exp;
        SetCtrl(subdev_, "exposure", exposure_);
      } else if (have_gain_ && gain_ < gain_hi_) {
        gain_ =
            std::min(static_cast<int>(std::max(gain_, 1) * ratio), gain_hi_);
        SetCtrl(subdev_, "analogue_gain", gain_);
      }
    } else {  // too bright: drop gain first, then exposure
      if (have_gain_ && gain_ > gain_lo_) {
        gain_ = std::max(static_cast<int>(gain_ * ratio), gain_lo_);
        SetCtrl(subdev_, "analogue_gain", gain_);
      } else {
        const int new_exp =
            std::max(static_cast<int>(exposure_ * ratio), exp_lo_);
        if (new_exp != exposure_) {
          exposure_ = new_exp;
          SetCtrl(subdev_, "exposure", exposure_);
        }
      }
    }
  }

 private:
  static constexpr double kDeadband = 6.0;
  static constexpr double kGainFactor = 0.6;
  std::string subdev_;
  int exp_lo_ = 0, exp_hi_ = 0, exposure_ = 0;
  bool have_gain_ = false;
  int gain_lo_ = 0, gain_hi_ = 0, gain_ = 0;
  int target_ = 110;
};

// Lightweight UDP sender to a fixed destination.
class UdpSender {
 public:
  absl::Status Open(const std::string& host, int port) {
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) {
      return absl::InternalError("Failed to create UDP socket");
    }
    std::memset(&dest_, 0, sizeof(dest_));
    dest_.sin_family = AF_INET;
    dest_.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &dest_.sin_addr) != 1) {
      return absl::InvalidArgumentError("Bad UDP host: " + host);
    }
    return absl::OkStatus();
  }

  void Send(const std::string& msg) {
    if (sock_ < 0) return;
    sendto(sock_, msg.data(), msg.size(), 0,
           reinterpret_cast<const struct sockaddr*>(&dest_), sizeof(dest_));
  }

  ~UdpSender() {
    if (sock_ >= 0) close(sock_);
  }

 private:
  int sock_ = -1;
  struct sockaddr_in dest_;
};

using Clock = std::chrono::steady_clock;

}  // namespace

absl::Status RunMPPGraph() {
  // 1. Load and initialise the graph.
  std::string graph_config_contents;
  MP_RETURN_IF_ERROR(mediapipe::file::GetContents(
      absl::GetFlag(FLAGS_calculator_graph_config_file),
      &graph_config_contents));
  mediapipe::CalculatorGraphConfig config =
      mediapipe::ParseTextProtoOrDie<mediapipe::CalculatorGraphConfig>(
          graph_config_contents);

  mediapipe::CalculatorGraph graph;
  MP_RETURN_IF_ERROR(graph.Initialize(config));

  // 2. Open the UDP sender.
  UdpSender udp;
  MP_RETURN_IF_ERROR(
      udp.Open(absl::GetFlag(FLAGS_udp_host), absl::GetFlag(FLAGS_udp_port)));

  // Shared throttle state (observer runs on a graph thread).
  auto last_send = Clock::now() - std::chrono::hours(1);
  auto last_hand = Clock::now() - std::chrono::hours(1);
  const auto interval =
      std::chrono::milliseconds(absl::GetFlag(FLAGS_send_interval_ms));

  // Latest gesture summary, shared with the capture loop for the preview
  // overlay (the observer runs on a separate graph thread).
  std::mutex summary_mutex;
  gesture::HandSummary latest_summary;

  // 3. Observe the landmarks stream. It only emits when hands are present, so a
  //    "no hand" timeout is handled in the capture loop below.
  MP_RETURN_IF_ERROR(graph.ObserveOutputStream(
      kLandmarksStream, [&](const mediapipe::Packet& packet) -> absl::Status {
        const auto& hands =
            packet.Get<std::vector<mediapipe::NormalizedLandmarkList>>();
        if (hands.empty()) return absl::OkStatus();

        last_hand = Clock::now();
        if (last_hand - last_send < interval) return absl::OkStatus();
        last_send = last_hand;

        const gesture::HandSummary summary = gesture::AnalyzeHand(hands.front());
        {
          std::lock_guard<std::mutex> lock(summary_mutex);
          latest_summary = summary;
        }
        udp.Send(gesture::FormatSummary(summary));
        return absl::OkStatus();
      }));

  // 4. Poll output_video purely to pace the loop (image is discarded).
  MP_ASSIGN_OR_RETURN(mediapipe::OutputStreamPoller poller,
                      graph.AddOutputStreamPoller(kOutputStream));
  MP_RETURN_IF_ERROR(graph.StartRun({}));

  // 5. Open the camera (or video file).
  cv::VideoCapture capture;
  const bool load_video = !absl::GetFlag(FLAGS_input_video_path).empty();
  if (load_video) {
    capture.open(absl::GetFlag(FLAGS_input_video_path));
  } else if (absl::GetFlag(FLAGS_use_gstreamer)) {
    // CSI IMX219 on the BeagleY-AI: raw Bayer + software debayer (no ISP).
    const std::string pipeline = BuildGstPipeline(
        absl::GetFlag(FLAGS_camera_device), absl::GetFlag(FLAGS_bayer_format),
        absl::GetFlag(FLAGS_sensor_width), absl::GetFlag(FLAGS_sensor_height),
        absl::GetFlag(FLAGS_capture_width), absl::GetFlag(FLAGS_capture_height));
    capture.open(pipeline, cv::CAP_GSTREAMER);
  } else {
    // USB camera fallback via the plain V4L2 backend.
    capture.open(absl::GetFlag(FLAGS_camera_index));
    capture.set(cv::CAP_PROP_FRAME_WIDTH, absl::GetFlag(FLAGS_capture_width));
    capture.set(cv::CAP_PROP_FRAME_HEIGHT, absl::GetFlag(FLAGS_capture_height));
    capture.set(cv::CAP_PROP_FPS, 30);
  }
  RET_CHECK(capture.isOpened()) << "Could not open camera/video source.";

  // Software auto-exposure for the ISP-less CSI IMX219 (skipped for USB/video).
  SoftwareAe ae;
  bool ae_active = false;
  if (!load_video && absl::GetFlag(FLAGS_use_gstreamer) &&
      absl::GetFlag(FLAGS_auto_exposure)) {
    ae_active = ae.Init(
        absl::GetFlag(FLAGS_sensor_subdev), absl::GetFlag(FLAGS_ae_vblank),
        absl::GetFlag(FLAGS_ae_digital_gain), absl::GetFlag(FLAGS_ae_target),
        absl::GetFlag(FLAGS_ae_max_gain));
    if (ae_active) {
      ABSL_LOG(INFO) << "Software AE active on " << ae.subdev()
                     << " (target luma " << absl::GetFlag(FLAGS_ae_target)
                     << ").";
    } else {
      ABSL_LOG(WARNING) << "Software AE requested but no sensor sub-device with "
                           "an 'exposure' control was found; image may be dark.";
    }
  }
  const int ae_interval = std::max(1, absl::GetFlag(FLAGS_ae_interval));
  int ae_frame = 0;

  ABSL_LOG(INFO) << "Gesture recogniser running; sending UDP to "
                 << absl::GetFlag(FLAGS_udp_host) << ":"
                 << absl::GetFlag(FLAGS_udp_port);

  // 5b. Optional low-latency MJPEG/RTP preview stream to a remote host. The
  //     board is headless, so this lets you watch the annotated frames on a
  //     laptop. jpegenc is intra-frame (no GOP latency); the leaky queue keeps
  //     only the newest frame and sync=false avoids clock buffering, so the
  //     feed stays near real time instead of building a backlog. The encode is
  //     a small 640x480 JPEG on its own GStreamer thread, so on the quad-core
  //     AM67A it costs little of the inference budget.
  cv::VideoWriter streamer;
  const std::string stream_host = absl::GetFlag(FLAGS_stream_host);
  if (!stream_host.empty()) {
    // rtpjpegpay (RFC 2435) only accepts YUV 4:2:0/4:2:2 JPEGs, so force I420
    // before jpegenc; otherwise it rejects every frame ("Invalid component")
    // and nothing is sent.
    const std::string send_pipeline =
        "appsrc ! videoconvert ! video/x-raw,format=I420 ! "
        "queue leaky=downstream max-size-buffers=1 ! "
        "jpegenc ! rtpjpegpay ! udpsink host=" +
        stream_host + " port=" + std::to_string(absl::GetFlag(FLAGS_stream_port)) +
        " sync=false";
    streamer.open(send_pipeline, cv::CAP_GSTREAMER, /*fourcc=*/0, /*fps=*/30,
                  cv::Size(absl::GetFlag(FLAGS_capture_width),
                           absl::GetFlag(FLAGS_capture_height)),
                  /*isColor=*/true);
    if (!streamer.isOpened()) {
      ABSL_LOG(WARNING) << "Could not open preview stream to " << stream_host
                        << "; continuing without it.";
    } else {
      ABSL_LOG(INFO) << "Streaming annotated preview to " << stream_host << ":"
                     << absl::GetFlag(FLAGS_stream_port);
    }
  }

  // 6. Capture loop.
  const bool rotate180 = absl::GetFlag(FLAGS_rotate180);
  const bool white_balance = absl::GetFlag(FLAGS_white_balance);
  bool grab_frames = true;
  while (grab_frames) {
    cv::Mat camera_frame_raw;
    capture >> camera_frame_raw;
    if (camera_frame_raw.empty()) {
      if (load_video) break;  // end of file
      continue;               // transient camera hiccup
    }

    // Software AE: periodically nudge sensor exposure/gain toward target luma.
    if (ae_active && (ae_frame++ % ae_interval == 0)) ae.Update(camera_frame_raw);

    // Correct orientation/colour before handing the frame to the graph.
    if (rotate180) cv::rotate(camera_frame_raw, camera_frame_raw, cv::ROTATE_180);
    if (white_balance) ApplyGrayWorld(camera_frame_raw);

    cv::Mat camera_frame;
    cv::cvtColor(camera_frame_raw, camera_frame, cv::COLOR_BGR2RGB);

    auto input_frame = absl::make_unique<mediapipe::ImageFrame>(
        mediapipe::ImageFormat::SRGB, camera_frame.cols, camera_frame.rows,
        mediapipe::ImageFrame::kDefaultAlignmentBoundary);
    cv::Mat input_frame_mat = mediapipe::formats::MatView(input_frame.get());
    camera_frame.copyTo(input_frame_mat);

    const size_t frame_timestamp_us =
        static_cast<double>(cv::getTickCount()) / cv::getTickFrequency() * 1e6;
    MP_RETURN_IF_ERROR(graph.AddPacketToInputStream(
        kInputStream, mediapipe::Adopt(input_frame.release())
                          .At(mediapipe::Timestamp(frame_timestamp_us))));

    // Pace the loop on the rendered output. When previewing, also push the
    // annotated frame (landmarks drawn) to the remote host; otherwise it is
    // simply discarded as before.
    mediapipe::Packet output_packet;
    if (!poller.Next(&output_packet)) break;
    if (streamer.isOpened()) {
      const auto& output_frame = output_packet.Get<mediapipe::ImageFrame>();
      const cv::Mat output_mat = mediapipe::formats::MatView(&output_frame);
      cv::Mat preview;
      cv::cvtColor(output_mat, preview, cv::COLOR_RGB2BGR);

      // Overlay the recognised gesture name + finger count so it shows on the
      // remote preview (drawn twice: black outline then green fill for legibility).
      gesture::HandSummary snap;
      {
        std::lock_guard<std::mutex> lock(summary_mutex);
        snap = latest_summary;
      }
      const std::string label =
          snap.present ? snap.name + " (" + std::to_string(snap.count) + ")"
                       : "NONE";
      cv::putText(preview, label, cv::Point(12, 34), cv::FONT_HERSHEY_SIMPLEX,
                  1.0, cv::Scalar(0, 0, 0), 4, cv::LINE_AA);
      cv::putText(preview, label, cv::Point(12, 34), cv::FONT_HERSHEY_SIMPLEX,
                  1.0, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
      streamer.write(preview);
    }

    // No-hand timeout: if no landmarks for a while, emit a NONE summary.
    const auto now = Clock::now();
    if (now - last_hand > std::chrono::milliseconds(300) &&
        now - last_send >= interval) {
      last_send = now;
      {
        std::lock_guard<std::mutex> lock(summary_mutex);
        latest_summary = gesture::NoHand();
      }
      udp.Send(gesture::FormatSummary(gesture::NoHand()));
    }
  }

  ABSL_LOG(INFO) << "Shutting down gesture recogniser.";
  MP_RETURN_IF_ERROR(graph.CloseInputStream(kInputStream));
  return graph.WaitUntilDone();
}

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  const absl::Status run_status = RunMPPGraph();
  if (!run_status.ok()) {
    ABSL_LOG(ERROR) << "Failed to run the graph: " << run_status.message();
    return EXIT_FAILURE;
  }
  ABSL_LOG(INFO) << "Success!";
  return EXIT_SUCCESS;
}
