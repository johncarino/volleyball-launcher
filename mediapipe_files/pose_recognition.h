// pose_recognition.h
//
// Tiny, dependency-light helper that turns a single MediaPipe BlazePose result
// (33 normalised landmarks) into the launcher's "start" signal: whether a
// person is present and whether an arm is raised (a wrist above the matching
// shoulder). Used by m2demo.cpp for the BeagleY-AI gesture-control project.

#ifndef MEDIAPIPE_MEDIAPIPE_FILES_POSE_RECOGNITION_H_
#define MEDIAPIPE_MEDIAPIPE_FILES_POSE_RECOGNITION_H_

#include <string>

#include "mediapipe/framework/formats/landmark.pb.h"

namespace gesture {

// Minimal pose summary for the launcher's raised-arm start signal.
struct PoseSummary {
  bool present = false;     // a person is visible
  bool arm_raised = false;  // a wrist is above its shoulder
};

// Region of interest (normalised image coords, 0..1, y grows downward). When
// enabled, a pose only counts as present if its anchor (the midpoint of the
// visible shoulders) lies inside [x_min,x_max]x[y_min,y_max]. This lets the
// launcher ignore bystanders standing outside the on-court launch spot -- e.g.
// people waiting in a hitting line off to the sides of the frame.
struct PoseRoi {
  float x_min = 0.0f;
  float x_max = 1.0f;
  float y_min = 0.0f;
  float y_max = 1.0f;
  bool enabled = false;  // false accepts the whole frame (no gating)
};

// Analyse one BlazePose landmark list (33 normalised landmarks, image coords,
// y grows downward). "Arm raised" means either wrist is above (smaller y than)
// its shoulder by a small margin, with both landmarks sufficiently visible.
PoseSummary AnalyzePose(const mediapipe::NormalizedLandmarkList& landmarks);

// As above, but report the pose as absent when its shoulder-midpoint anchor
// falls outside the region of interest.
PoseSummary AnalyzePose(const mediapipe::NormalizedLandmarkList& landmarks,
                        const PoseRoi& roi);

// A "no person present" summary.
PoseSummary NoPose();

// Wire protocol line sent over UDP to the Node backend:
//   "pose <present> <arm_raised>"  (each field 0/1), e.g. "pose 1 1".
std::string FormatSummary(const PoseSummary& s);

}  // namespace gesture

#endif  // MEDIAPIPE_MEDIAPIPE_FILES_POSE_RECOGNITION_H_
