#include "Parameters.h"

namespace openglitch::params {

juce::String stepParamId(int stepIndex) {
    return juce::String::formatted("step_%02d", stepIndex + 1);
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> layout;

    layout.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ kTheme, 1 },
        "Look Theme",
        juce::StringArray{ "Cyber Neon", "Industrial Amber", "Acid Matrix", "Arctic Glitch" },
        0));

    layout.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ kHostSync, 1 },
        "Host Beat Sync",
        true));

    layout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ kInternalBpm, 1 },
        "Internal BPM",
        juce::NormalisableRange<float>(40.0f, 240.0f, 0.1f),
        120.0f,
        juce::AudioParameterFloatAttributes().withLabel("BPM")));

    layout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ kSmoothMs, 1 },
        "Gate Smooth",
        juce::NormalisableRange<float>(0.5f, 25.0f, 0.1f, 0.6f),
        2.5f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // 32 Selectable Spots in a 4/4 Bar (Default: Last Beat [Steps 25..32] enabled)
    for (int i = 0; i < 32; ++i) {
        const bool defaultActive = (i >= 24);
        const auto id = stepParamId(i);
        const auto name = juce::String::formatted("Bar Step %02d/32", i + 1);
        layout.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{ id, 1 },
            name,
            defaultActive));
    }

    auto addFxParam = [&](const char* amtId, const char* onId, const char* title, float defaultPct) {
        layout.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ amtId, 1 },
            juce::String(title) + " Amount",
            juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
            defaultPct,
            juce::AudioParameterFloatAttributes().withLabel("%")));

        layout.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{ onId, 1 },
            juce::String(title) + " Enable",
            true));
    };

    // Phase 1: Initial 5 Glitch FX
    addFxParam(kBitcrushAmt,    kBitcrushOn,    "Bitrate Crush", 35.0f);
    addFxParam(kDownsampleAmt,  kDownsampleOn,  "Downsample",    0.0f);
    addFxParam(kStutterAmt,     kStutterOn,     "Beat Stutter",  0.0f);
    addFxParam(kTapeStopAmt,    kTapeStopOn,    "Tape Stop",     0.0f);
    addFxParam(kRingModAmt,     kRingModOn,     "Ring Mod",      0.0f);

    // Phase 2: 5 Additional Glitch FX (10 Total)
    addFxParam(kReverseAmt,     kReverseOn,     "Reverse Grain", 0.0f);
    addFxParam(kChopperAmt,     kChopperOn,     "Chopper Gate",  0.0f);
    addFxParam(kPitchGlitchAmt, kPitchGlitchOn, "Pitch Glitch",  0.0f);
    addFxParam(kCombResoAmt,    kCombResoOn,    "Comb Resonator",0.0f);
    addFxParam(kWavefoldAmt,    kWavefoldOn,    "Wavefolder",    0.0f);

    // Master Controls
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ kMasterMix, 1 },
        "Master Wet Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        100.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ kMasterGain, 1 },
        "Output Gain",
        juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    layout.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ kMasterPower, 1 },
        "Master Power",
        true));

    return { layout.begin(), layout.end() };
}

} // namespace openglitch::params
