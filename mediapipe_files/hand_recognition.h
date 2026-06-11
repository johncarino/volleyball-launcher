// hand_recognition.h
//
// Tiny, dependency-light helper that turns a single MediaPipe hand
// (21 normalised landmarks) into a "which fingers are up" summary plus a
// named gesture. Used by m2demo.cpp for the BeagleY-AI gesture-control project.

#ifndef MEDIAPIPE_MEDIAPIPE_FILES_HAND_RECOGNITION_H_
#define MEDIAPIPE_MEDIAPIPE_FILES_HAND_RECOGNITION_H_

#include <array>
#include <string>

#include "mediapipe/framework/formats/landmark.pb.h"

namespace gesture {

// fingers_up order: [0]=thumb, [1]=index, [2]=middle, [3]=ring, [4]=pinky.
struct HandSummary {
  bool present = false;
  std::array<bool, 5> fingers_up = {{false, false, false, false, false}};
  int count = 0;            // number of extended fingers
  std::string name = "NONE";
};

// Analyse one hand's 21 normalised landmarks (image coords, y grows downward).
// Assumes a roughly upright hand (fingers pointing up), which is adequate for a
// barebones gesture base. Thumb detection is handedness-independent.
HandSummary AnalyzeHand(const mediapipe::NormalizedLandmarkList& landmarks);

// A "no hand present" summary.
HandSummary NoHand();

// Wire protocol line sent over UDP to the Node backend:
//   "gesture <count> <thumb> <index> <middle> <ring> <pinky> <NAME>"
// e.g. "gesture 2 0 1 1 0 0 PEACE"
std::string FormatSummary(const HandSummary& s);

}  // namespace gesture

#endif  // MEDIAPIPE_MEDIAPIPE_FILES_HAND_RECOGNITION_H_
