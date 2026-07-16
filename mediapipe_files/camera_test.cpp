// camera_test.cpp
//
// Standalone "HAL-style" camera test for the BeagleY-AI gesture-control project.
//
// It isolates the one thing the gesture recogniser (m2demo) depends on but never
// validates on its own: that frames can actually be pulled off the CSI camera.
// On the BeagleY-AI the IMX219 is a raw-Bayer sensor and there is no working
// libcamera/ISP path, so the supported route is: capture raw RGGB Bayer from
// /dev/video-imx219-cam0 and debayer + scale to BGR in software via GStreamer
// (v4l2src -> bayer2rgb -> videoconvert/videoscale), consumed by OpenCV's
// CAP_GSTREAMER backend. This mirrors how m2demo opens the camera, so a pass
// here means the recogniser's capture path is good.
//
// Deliberately dependency-light: plain OpenCV only (no Bazel / MediaPipe / absl),
// so it compiles natively on the board with a single g++ command:
//   g++ -O2 camera_test.cpp -o camera_test $(pkg-config --cflags --libs opencv4)
//
// Run:
//   ./camera_test
//   ./camera_test --capture_width=1280 --capture_height=720 --frames=60
//   ./camera_test --bayer_format=grbg
//   ./camera_test --save=frame.jpg          # write a captured frame to inspect it
//   ./camera_test --rotate180=false         # disable the 180° flip
//   ./camera_test --white_balance=true       # apply gray-world white balance
//   ./camera_test --use_gstreamer=false --camera_index=0   # USB fallback

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

#include <opencv2/opencv.hpp>

namespace {

// Raw RGGB Bayer from the TI CSI node, debayered + scaled to BGR in software.
// The BeagleY-AI has no working libcamera/ISP path, so this is the supported
// route: v4l2src -> bayer2rgb -> videoconvert/videoscale -> BGR appsink.
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

// Apply the post-capture corrections m2demo also uses (orientation + colour).
void Postprocess(cv::Mat& bgr, bool rotate180, bool white_balance) {
  if (rotate180) cv::rotate(bgr, bgr, cv::ROTATE_180);
  if (white_balance) ApplyGrayWorld(bgr);
}

// --- Software auto-exposure for the ISP-less CSI IMX219 -------------------
// The BeagleY-AI has no ISP, so nothing runs the usual auto-exposure loop. We
// do it in software (ported from auto_exposure.py, same as m2demo): find the
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
  (void)rc;  // best-effort
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
// secondary one. Mirrors the control law in auto_exposure.py / m2demo.cpp.
class SoftwareAe {
 public:
  bool Init(const std::string& subdev, int vblank, int digital_gain, int target,
            int max_gain) {
    subdev_ = subdev.empty() ? FindSensorSubdev() : subdev;
    if (subdev_.empty()) return false;
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

// Minimal "--key=value" argument parser (no external flag library needed).
struct Options {
  bool use_gstreamer = true;
  std::string camera_device = "/dev/video-imx219-cam0";
  std::string bayer_format = "rggb";
  int sensor_width = 1920;
  int sensor_height = 1080;
  int camera_index = 0;
  int capture_width = 640;
  int capture_height = 480;
  int capture_fps = 30;
  int frames = 30;
  bool rotate180 = true;       // camera is mounted upside-down
  bool white_balance = true;   // gray-world AWB (on by default, matches m2demo)
  bool auto_exposure = true;   // software AE for the ISP-less IMX219
  std::string sensor_subdev;   // AE sub-device (empty = auto-detect)
  int ae_target = 110;         // target mean luma 0..255
  int ae_vblank = 6000;        // raises exposure ceiling (higher = brighter/slower)
  int ae_digital_gain = 256;   // 256=1.0x .. 4095=16x
  int ae_max_gain = 0;         // cap analogue_gain (0 = driver max)
  std::string save_path;  // if set, write a grabbed frame here (e.g. frame.jpg)
};

bool MatchFlag(const std::string& arg, const char* key, std::string* value) {
  const std::string prefix = std::string("--") + key + "=";
  if (arg.rfind(prefix, 0) == 0) {
    *value = arg.substr(prefix.size());
    return true;
  }
  return false;
}

Options ParseArgs(int argc, char** argv) {
  Options o;
  std::string v;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (MatchFlag(arg, "use_gstreamer", &v)) {
      o.use_gstreamer = (v == "1" || v == "true" || v == "yes");
    } else if (MatchFlag(arg, "camera_device", &v)) {
      o.camera_device = v;
    } else if (MatchFlag(arg, "bayer_format", &v)) {
      o.bayer_format = v;
    } else if (MatchFlag(arg, "sensor_width", &v)) {
      o.sensor_width = std::atoi(v.c_str());
    } else if (MatchFlag(arg, "sensor_height", &v)) {
      o.sensor_height = std::atoi(v.c_str());
    } else if (MatchFlag(arg, "camera_index", &v)) {
      o.camera_index = std::atoi(v.c_str());
    } else if (MatchFlag(arg, "capture_width", &v)) {
      o.capture_width = std::atoi(v.c_str());
    } else if (MatchFlag(arg, "capture_height", &v)) {
      o.capture_height = std::atoi(v.c_str());
    } else if (MatchFlag(arg, "capture_fps", &v)) {
      o.capture_fps = std::atoi(v.c_str());
    } else if (MatchFlag(arg, "frames", &v)) {
      o.frames = std::atoi(v.c_str());
    } else if (MatchFlag(arg, "rotate180", &v)) {
      o.rotate180 = (v == "1" || v == "true" || v == "yes");
    } else if (MatchFlag(arg, "white_balance", &v)) {
      o.white_balance = (v == "1" || v == "true" || v == "yes");
    } else if (MatchFlag(arg, "auto_exposure", &v)) {
      o.auto_exposure = (v == "1" || v == "true" || v == "yes");
    } else if (MatchFlag(arg, "sensor_subdev", &v)) {
      o.sensor_subdev = v;
    } else if (MatchFlag(arg, "ae_target", &v)) {
      o.ae_target = std::atoi(v.c_str());
    } else if (MatchFlag(arg, "ae_vblank", &v)) {
      o.ae_vblank = std::atoi(v.c_str());
    } else if (MatchFlag(arg, "ae_digital_gain", &v)) {
      o.ae_digital_gain = std::atoi(v.c_str());
    } else if (MatchFlag(arg, "ae_max_gain", &v)) {
      o.ae_max_gain = std::atoi(v.c_str());
    } else if (MatchFlag(arg, "save", &v)) {
      o.save_path = v;
    } else {
      std::cerr << "Ignoring unknown argument: " << arg << "\n";
    }
  }
  return o;
}

}  // namespace

int main(int argc, char** argv) {
  const Options opt = ParseArgs(argc, argv);

  cv::VideoCapture capture;
  if (opt.use_gstreamer) {
    const std::string pipeline = BuildGstPipeline(
        opt.camera_device, opt.bayer_format, opt.sensor_width, opt.sensor_height,
        opt.capture_width, opt.capture_height);
    std::cout << "Opening camera via GStreamer:\n  " << pipeline << "\n";
    capture.open(pipeline, cv::CAP_GSTREAMER);
  } else {
    std::cout << "Opening camera via V4L2: /dev/video" << opt.camera_index
              << "\n";
    capture.open(opt.camera_index);
    capture.set(cv::CAP_PROP_FRAME_WIDTH, opt.capture_width);
    capture.set(cv::CAP_PROP_FRAME_HEIGHT, opt.capture_height);
    capture.set(cv::CAP_PROP_FPS, opt.capture_fps);
  }

  if (!capture.isOpened()) {
    std::cerr << "FAIL: could not open the camera.\n"
              << "  - For the CSI IMX219, check this works first:\n"
                 "      gst-launch-1.0 v4l2src device=/dev/video-imx219-cam0 ! "
                 "video/x-bayer,format=rggb,width=1920,height=1080 ! "
                 "bayer2rgb ! videoconvert ! fakesink\n"
              << "  - If gst-launch works but this does not, your OpenCV was "
                 "built without GStreamer support\n"
              << "    (check cv::getBuildInformation() -> 'GStreamer: NO').\n";
    return 1;
  }

  int grabbed = 0;
  cv::Mat last_frame;
  SoftwareAe ae;
  bool ae_active = false;
  if (opt.use_gstreamer && opt.auto_exposure) {
    ae_active = ae.Init(opt.sensor_subdev, opt.ae_vblank, opt.ae_digital_gain,
                        opt.ae_target, opt.ae_max_gain);
    if (ae_active) {
      std::cout << "Software AE active on " << ae.subdev() << " (target luma "
                << opt.ae_target << ").\n";
    } else {
      std::cerr << "WARN: software AE requested but no sensor sub-device with an "
                   "'exposure' control was found; image may be dark.\n";
    }
  }
  for (int i = 0; i < opt.frames; ++i) {
    cv::Mat frame;
    capture >> frame;
    if (!frame.empty()) {
      if (ae_active) ae.Update(frame);
      ++grabbed;
      last_frame = frame;
    }
  }

  const double actual_w = capture.get(cv::CAP_PROP_FRAME_WIDTH);
  const double actual_h = capture.get(cv::CAP_PROP_FRAME_HEIGHT);
  std::cout << "Grabbed " << grabbed << "/" << opt.frames << " frames at "
            << actual_w << "x" << actual_h << ".\n";

  if (grabbed == 0) {
    std::cerr << "FAIL: camera opened but delivered no frames.\n";
    return 2;
  }

  if (!opt.save_path.empty()) {
    Postprocess(last_frame, opt.rotate180, opt.white_balance);
    if (cv::imwrite(opt.save_path, last_frame)) {
      std::cout << "Saved a frame to " << opt.save_path << ".\n";
    } else {
      std::cerr << "WARN: failed to write " << opt.save_path << ".\n";
    }
  }

  std::cout << "PASS: camera is delivering frames.\n";
  return 0;
}
