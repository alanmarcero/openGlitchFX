#pragma once

#include <array>
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

// Phase 1: Initial 5 Glitch FX IDs
inline constexpr const char* kBitcrushAmt    = "bitcrush_amt";
inline constexpr const char* kBitcrushOn     = "bitcrush_on";

inline constexpr const char* kDownsampleAmt  = "downsample_amt";
inline constexpr const char* kDownsampleOn   = "downsample_on";

inline constexpr const char* kStutterAmt     = "stutter_amt";
inline constexpr const char* kStutterOn      = "stutter_on";

inline constexpr const char* kTapeStopAmt    = "tapestop_amt";
inline constexpr const char* kTapeStopOn     = "tapestop_on";

inline constexpr const char* kRingModAmt     = "ringmod_amt";
inline constexpr const char* kRingModOn      = "ringmod_on";

// Phase 2: Additional 5 Glitch FX IDs (10 Total)
inline constexpr const char* kReverseAmt     = "reverse_amt";
inline constexpr const char* kReverseOn      = "reverse_on";

inline constexpr const char* kChopperAmt     = "chopper_amt";
inline constexpr const char* kChopperOn      = "chopper_on";

inline constexpr const char* kPitchGlitchAmt = "pitchglitch_amt";
inline constexpr const char* kPitchGlitchOn  = "pitchglitch_on";

inline constexpr const char* kCombResoAmt    = "combreso_amt";
inline constexpr const char* kCombResoOn     = "combreso_on";

inline constexpr const char* kWavefoldAmt    = "wavefold_amt";
inline constexpr const char* kWavefoldOn     = "wavefold_on";

struct FxDescriptor {
    const char* name;
    const char* shortCode;
    const char* amtParamId;
    const char* onParamId;
};

inline constexpr std::array<FxDescriptor, 10> kFxDescriptors = {{
    { "BITCRUSH",     "BC", kBitcrushAmt,    kBitcrushOn    },
    { "DOWNSAMPLE",   "DS", kDownsampleAmt,  kDownsampleOn  },
    { "STUTTER",      "ST", kStutterAmt,     kStutterOn     },
    { "TAPE STOP",    "TS", kTapeStopAmt,    kTapeStopOn    },
    { "RING MOD",     "RM", kRingModAmt,     kRingModOn     },
    { "REVERSE",      "RV", kReverseAmt,     kReverseOn     },
    { "CHOPPER",      "CH", kChopperAmt,     kChopperOn     },
    { "PITCH GLITCH", "PG", kPitchGlitchAmt, kPitchGlitchOn },
    { "COMB RESO",    "CR", kCombResoAmt,    kCombResoOn    },
    { "WAVEFOLD",     "WF", kWavefoldAmt,    kWavefoldOn    }
}};

juce::String stepParamId(int stepIndex);   // 0..31 -> "step_01".."step_32"
juce::String stepFxParamId(int stepIndex); // 0..31 -> "step_fx_01".."step_fx_32"

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace openglitch::params
