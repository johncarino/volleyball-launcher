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
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
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
ABSL_FLAG(int, camera_index, 0,
          "OpenCV camera index, i.e. N for /dev/videoN (CSI or USB).");
ABSL_FLAG(int, capture_width, 640, "Requested camera capture width.");
ABSL_FLAG(int, capture_height, 480, "Requested camera capture height.");
ABSL_FLAG(std::string, udp_host, "127.0.0.1",
          "Host to send gesture summaries to.");
ABSL_FLAG(int, udp_port, 12345, "UDP port to send gesture summaries to.");
ABSL_FLAG(int, send_interval_ms, 100,
          "Minimum interval between UDP sends (throttle, default ~10 Hz).");

namespace {

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
  } else {
    capture.open(absl::GetFlag(FLAGS_camera_index));
    capture.set(cv::CAP_PROP_FRAME_WIDTH, absl::GetFlag(FLAGS_capture_width));
    capture.set(cv::CAP_PROP_FRAME_HEIGHT, absl::GetFlag(FLAGS_capture_height));
    capture.set(cv::CAP_PROP_FPS, 30);
  }
  RET_CHECK(capture.isOpened()) << "Could not open camera/video source.";

  ABSL_LOG(INFO) << "Gesture recogniser running; sending UDP to "
                 << absl::GetFlag(FLAGS_udp_host) << ":"
                 << absl::GetFlag(FLAGS_udp_port);

  // 6. Capture loop.
  bool grab_frames = true;
  while (grab_frames) {
    cv::Mat camera_frame_raw;
    capture >> camera_frame_raw;
    if (camera_frame_raw.empty()) {
      if (load_video) break;  // end of file
      continue;               // transient camera hiccup
    }

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

    // Pace the loop on the rendered output (discarded).
    mediapipe::Packet output_packet;
    if (!poller.Next(&output_packet)) break;

    // No-hand timeout: if no landmarks for a while, emit a NONE summary.
    const auto now = Clock::now();
    if (now - last_hand > std::chrono::milliseconds(300) &&
        now - last_send >= interval) {
      last_send = now;
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
