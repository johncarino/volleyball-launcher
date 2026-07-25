// hand_recognition.cpp
//
// Implementation of the hand -> gesture summary logic. See hand_recognition.h.

#include "mediapipe/mediapipe_files/hand_recognition.h"

#include <cmath>
#include <string>

namespace gesture {

namespace {

// MediaPipe hand landmark indices.
enum Lm {
  kWrist = 0,
  kThumbCmc = 1,
  kThumbMcp = 2,
  kThumbIp = 3,
  kThumbTip = 4,
  kIndexMcp = 5,
  kIndexPip = 6,
  kIndexTip = 8,
  kMiddlePip = 10,
  kMiddleTip = 12,
  kRingPip = 14,
  kRingTip = 16,
  kPinkyMcp = 17,
  kPinkyPip = 18,
  kPinkyTip = 20,
};

float Dist2D(const mediapipe::NormalizedLandmark& a,
             const mediapipe::NormalizedLandmark& b) {
  const float dx = a.x() - b.x();
  const float dy = a.y() - b.y();
  return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

HandSummary NoHand() { return HandSummary{}; }

HandSummary AnalyzeHand(const mediapipe::NormalizedLandmarkList& lm) {
  HandSummary s;
  if (lm.landmark_size() < 21) {
    return s;  // not a full hand; treat as absent
  }
  s.present = true;

  // Thumb: extended when the tip is farther from the pinky MCP than the IP
  // joint is. This is independent of left/right handedness and of image flip.
  const float d_tip = Dist2D(lm.landmark(kThumbTip), lm.landmark(kPinkyMcp));
  const float d_ip = Dist2D(lm.landmark(kThumbIp), lm.landmark(kPinkyMcp));
  s.fingers_up[0] = d_tip > d_ip;

  // Four fingers: extended when the tip is above (smaller y) its PIP joint.
  s.fingers_up[1] = lm.landmark(kIndexTip).y() < lm.landmark(kIndexPip).y();
  s.fingers_up[2] = lm.landmark(kMiddleTip).y() < lm.landmark(kMiddlePip).y();
  s.fingers_up[3] = lm.landmark(kRingTip).y() < lm.landmark(kRingPip).y();
  s.fingers_up[4] = lm.landmark(kPinkyTip).y() < lm.landmark(kPinkyPip).y();

  for (bool up : s.fingers_up) {
    if (up) s.count++;
  }

  // t = thumb, i = index, m = middle, r = ring, p = pinky
  const bool t = s.fingers_up[0];
  const bool i = s.fingers_up[1];
  const bool m = s.fingers_up[2];
  const bool r = s.fingers_up[3];
  const bool p = s.fingers_up[4];

  if (!t && !i && !m && !r && !p) {
    s.name = "FIST";
  } else if (t && i && m && r && p) {
    s.name = "OPEN_PALM";
  } else if (!t && i && !m && !r && !p) {
    s.name = "POINT";
  } else if (!t && i && m && !r && !p) {
    s.name = "PEACE";
  } else if (t && !i && !m && !r && !p) {
    s.name = "THUMBS_UP";
  } else if (t && !i && !m && !r && p) {
    s.name = "CALL_ME";
  } else if (!t && i && m && r && p) {
    s.name = "FOUR";
  } else if (t && i && !m && !r && !p) {
    s.name = "GUN";
  } else {
    s.name = "UNKNOWN";
  }

  return s;
}

std::string FormatSummary(const HandSummary& s) {
  std::string out = "gesture ";
  out += std::to_string(s.count);
  for (bool up : s.fingers_up) {
    out += ' ';
    out += (up ? '1' : '0');
  }
  out += ' ';
  out += s.name;
  return out;
}

}  // namespace gesture
