#pragma once
// Constants shared by the processor, the output engine and the editor.

#include <array>

namespace ab
{
constexpr int kNumChannels = 12;   // 7.1.4: 12 input channels and 12 output positions
constexpr int kMaxInputs   = 32;   // host channels the plugin accepts (only 1..12 are used)

// Output positions (= Windows static audio objects), numbered 1..12 in the UI.
// This is also the default input order: L R C LFE SBL SBR SL SR TFL TFR TRL TRR.
inline constexpr std::array<const char*, kNumChannels> kPosCodes {
    "L", "R", "C", "LFE", "SBL", "SBR", "SL", "SR", "TFL", "TFR", "TRL", "TRR"
};

constexpr int kLfePos = 3;              // position index of LFE (0-based)
constexpr float kNoiseDbfs = -20.0f;    // pink noise level, RMS (fixed)
constexpr double kLfeNoiseCutHz = 120.0;
constexpr double kSequenceSeconds = 2.0; // time per position in the sequential test

// Latency modes
enum class LatencyMode : int { Music = 0, Low = 1, Ultra = 2 };
constexpr int kNumModes = 3;

// Editor base size (everything is drawn in these coordinates and scaled).
constexpr int kBaseWidth  = 1280;
constexpr int kBaseHeight = 812;
} // namespace ab
