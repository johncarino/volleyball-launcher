// pose_recognition.cpp
//
// Implementation of the pose -> "raised arm" start signal. See
// pose_recognition.h.

#include "mediapipe/mediapipe_files/pose_recognition.h"

#include <string>

namespace gesture {

namespace {

// BlazePose full-body landmark indices (subset we need).
enum Lm {
  kLeftShoulder = 11,
  kRightShoulder = 12,
  kLeftWrist = 15,
  kRightWrist = 16,
};

// Minimum landmark visibility to trust it. BlazePose populates visibility in
// [0,1]; a value of 0 means "unpopulated" on some builds, so we treat that as
// usable rather than hiding the whole pose.
constexpr float kVisibility = 0.5f;
// A wrist must clear its shoulder by this normalised margin to count as raised,
// which debounces small jitter around shoulder height.
constexpr float kRaiseMargin = 0.02f;

bool Visible(const mediapipe::NormalizedLandmark& p) {
  return p.visibility() <= 0.0f || p.visibility() >= kVisibility;
}

}  // namespace

PoseSummary NoPose() { return PoseSummary{}; }

PoseSummary AnalyzePose(const mediapipe::NormalizedLandmarkList& lm) {
  return AnalyzePose(lm, PoseRoi{});
}

PoseSummary AnalyzePose(const mediapipe::NormalizedLandmarkList& lm,
                        const PoseRoi& roi) {
  PoseSummary s;
  if (lm.landmark_size() < 33) {
    return s;  // not a full pose; treat as absent
  }

  const mediapipe::NormalizedLandmark& ls = lm.landmark(kLeftShoulder);
  const mediapipe::NormalizedLandmark& rs = lm.landmark(kRightShoulder);
  const mediapipe::NormalizedLandmark& lw = lm.landmark(kLeftWrist);
  const mediapipe::NormalizedLandmark& rw = lm.landmark(kRightWrist);

  const bool ls_ok = Visible(ls);
  const bool rs_ok = Visible(rs);
  if (!ls_ok && !rs_ok) return s;  // no usable shoulders; treat as absent

  // Anchor = midpoint of the visible shoulders (a stable body centre). If a
  // region of interest is active and the anchor lies outside it, ignore this
  // person entirely so bystanders off to the sides can't trigger a launch.
  if (roi.enabled) {
    float ax, ay;
    if (ls_ok && rs_ok) {
      ax = 0.5f * (ls.x() + rs.x());
      ay = 0.5f * (ls.y() + rs.y());
    } else if (ls_ok) {
      ax = ls.x();
      ay = ls.y();
    } else {
      ax = rs.x();
      ay = rs.y();
    }
    if (ax < roi.x_min || ax > roi.x_max || ay < roi.y_min || ay > roi.y_max) {
      return s;  // outside the launch spot
    }
  }

  s.present = true;

  // y grows downward, so "above" means a smaller y than the shoulder.
  const bool left_up =
      Visible(lw) && ls_ok && lw.y() < ls.y() - kRaiseMargin;
  const bool right_up =
      Visible(rw) && rs_ok && rw.y() < rs.y() - kRaiseMargin;
  s.arm_raised = left_up || right_up;
  return s;
}

std::string FormatSummary(const PoseSummary& s) {
  std::string out = "pose ";
  out += (s.present ? '1' : '0');
  out += ' ';
  out += (s.arm_raised ? '1' : '0');
  return out;
}

}  // namespace gesture
