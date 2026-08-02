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
  PoseSummary s;
  if (lm.landmark_size() < 33) {
    return s;  // not a full pose; treat as absent
  }

  const mediapipe::NormalizedLandmark& ls = lm.landmark(kLeftShoulder);
  const mediapipe::NormalizedLandmark& rs = lm.landmark(kRightShoulder);
  const mediapipe::NormalizedLandmark& lw = lm.landmark(kLeftWrist);
  const mediapipe::NormalizedLandmark& rw = lm.landmark(kRightWrist);

  s.present = Visible(ls) || Visible(rs);
  if (!s.present) return s;

  // y grows downward, so "above" means a smaller y than the shoulder.
  const bool left_up =
      Visible(lw) && Visible(ls) && lw.y() < ls.y() - kRaiseMargin;
  const bool right_up =
      Visible(rw) && Visible(rs) && rw.y() < rs.y() - kRaiseMargin;
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
