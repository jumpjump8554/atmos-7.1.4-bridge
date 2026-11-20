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
// Output format (v1.5): Atmos = Windows spatial audio (Dolby Atmos for home theater, 12 objects);
// Stereo = PCM 2ch straight to the HDMI device (WASAPI exclusive) so the AVR's own upmixers can be used.
enum class OutputFormat : int { Atmos = 0, Stereo = 1 };
constexpr int kMaxFx = 3;     // 2ch plugins (applied in order before the upmixer / the 2ch output)

enum class LatencyMode : int { Music = 0, Low = 1, Ultra = 2 };
constexpr int kNumModes = 3;

constexpr float kLimiterCeilingDb = -1.0f; // clipping prevention ceiling (12 channels linked)
constexpr int kMaxUpmixers = 4;            // upmixer slots

// Theme presets (index stored in the project and, as "last chosen", for the whole PC user)
enum class ThemeId : int { Cyberpunk = 0, WhiteModern = 1, CoolBeaches = 2 };
constexpr int kNumThemes = 3;

// Editor base size (everything is drawn in these coordinates and scaled).
constexpr int kBaseWidth  = 1740;
constexpr int kBaseHeight = 812;
} // namespace ab
