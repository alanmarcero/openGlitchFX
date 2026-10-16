#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <string>

namespace openglitch::params {

inline constexpr const char* kTheme         = "theme";
inline constexpr const char* kHostSync      = "host_sync";
inline constexpr const char* kInternalBpm   = "internal_bpm";
inline constexpr const char* kSmoothMs      = "smooth_ms";
inline constexpr const char* kMasterMix     = "master_mix";
inline constexpr const char* kMasterGain    = "master_gain";
inline constexpr const char* kMasterPower   = "master_power";

// Phase 1 Glitch FX IDs (5 effects)
inline constexpr const char* kBitcrushAmt   = "bitcrush_amt";
inline constexpr const char* kBitcrushOn    = "bitcrush_on";

inline constexpr const char* kDownsampleAmt = "downsample_amt";
inline constexpr const char* kDownsampleOn  = "downsample_on";

inline constexpr const char* kStutterAmt    = "stutter_amt";
inline constexpr const char* kStutterOn     = "stutter_on";

inline constexpr const char* kTapeStopAmt   = "tapestop_amt";
inline constexpr const char* kTapeStopOn    = "tapestop_on";

inline constexpr const char* kRingModAmt    = "ringmod_amt";
inline constexpr const char* kRingModOn     = "ringmod_on";

juce::String stepParamId(int stepIndex); // 0..31 -> "step_01".."step_32"

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace openglitch::params
