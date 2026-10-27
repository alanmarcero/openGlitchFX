#include "Parameters.h"
#include "../Core/BeatStepSequencer.h"

namespace openglitch::params {

juce::String stepParamId(int stepIndex) {
    return juce::String::formatted("step_%02d", stepIndex + 1);
}

juce::String stepFxParamId(int stepIndex) {
    return juce::String::formatted("step_fx_%02d", stepIndex + 1);
}

juce::String stepExtraFxParamId(int stepIndex) {
    return juce::String::formatted("step_fx_extra_%02d", stepIndex + 1);
}

uint16_t readStepFxMask(const juce::AudioProcessorValueTreeState& state, int stepIndex) {
    const auto low = static_cast<uint16_t>(state.getRawParameterValue(stepFxParamId(stepIndex))->load());
    const auto high = static_cast<uint16_t>(state.getRawParameterValue(stepExtraFxParamId(stepIndex))->load());
    return static_cast<uint16_t>((low & 1023u) | ((high & 31u) << 10));
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

    // 32 Selectable Spots in a 4/4 Bar + Per-Pad 10-Bit FX Routing Mask
    constexpr int kAllFxInt = 1023; // Preserve the normalized automation range of v1.0/v1.1.
    for (int i = 0; i < 32; ++i) {
        const bool defaultActive = (i >= 24);
        const auto id = stepParamId(i);
        const auto name = juce::String::formatted("Bar Step %02d/32", i + 1);
        layout.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{ id, 1 },
            name,
            defaultActive));

        const auto fxMaskId = stepFxParamId(i);
        const auto fxMaskName = juce::String::formatted("Step %02d FX Mask", i + 1);
        layout.push_back(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID{ fxMaskId, 1 },
            fxMaskName,
            0,
            kAllFxInt,
            kAllFxInt));
    }

    auto addFxParam = [&](const char* amtId, const char* onId, const char* title, float defaultPct, int version = 1) {
        layout.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ amtId, version },
            juce::String(title) + " Amount",
            juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
            defaultPct,
            juce::AudioParameterFloatAttributes().withLabel("%")));

        layout.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{ onId, version },
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

    auto addChoice = [&](const char* id, const juce::String& title, const juce::StringArray& choices, int defaultIndex) {
        layout.push_back(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ id, 1 }, title, choices, defaultIndex));
    };
    auto addBool = [&](const char* id, const juce::String& title, bool defaultValue) {
        layout.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ id, 1 }, title, defaultValue));
    };
    auto addFloat = [&](const char* id, const juce::String& title, juce::NormalisableRange<float> range, float defaultValue, const char* label) {
        layout.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ id, 1 }, title, range, defaultValue, juce::AudioParameterFloatAttributes().withLabel(label)));
    };

    addChoice(kStutterSync,    "Buffer Size",    kStutterSyncLabels, 0);
    addBool  (kStutterAdvance, "Buffer Advance", false);
    addChoice(kStutterReverse, "Buffer Reverse", { "OFF", "ALL", "ALTERNATE" }, 0);
    addFloat (kStutterGate,    "Buffer Gate",    juce::NormalisableRange<float>(5.0f, 100.0f, 0.1f), 100.0f, "%");
    addFloat (kStutterPitch,   "Buffer Pitch",   juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f, "st");

    addChoice(kTapeDirection, "Tape Direction", { "STOP", "START" }, 0);
    addChoice(kTapeCurve,     "Tape Curve",     { "SMOOTH", "STEPPED" }, 0);
    addChoice(kTapeLength,    "Tape Length",    kNoteLengthLabels, 0);
    addBool  (kTapeSlip,      "Tape Slip",      false);
    addBool  (kTapeLowpass,   "Tape Low-Pass",  true);
    addBool  (kTapeFade,      "Tape Fade",      true);

    addChoice(kFilterMode,   "Filter Mode",      { "OFF", "LOW-PASS", "HIGH-PASS" }, 0);
    addFloat (kFilterCutoff, "Filter Cutoff",    juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.25f), 2000.0f, "Hz");
    addFloat (kFilterReso,   "Filter Resonance", juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 20.0f, "%");

    for (const auto& env : kEnvDescriptors) {
        const juce::String title(env.title);
        addBool  (env.onParamId,    title + " On",     false);
        addFloat (env.depthParamId, title + " Depth",  juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f, "%");
        addChoice(env.rateParamId,  title + " Length", kNoteLengthLabels, 0);
    }

    addChoice(kTriggerMode,  "Trigger Source", { "SEQUENCER", "MIDI" }, 0);
    addChoice(kTriggerStart, "MIDI Start",     { "FREE", "SYNCED" }, 0);
    addChoice(kMixMode,      "Dry/Wet Mode",   { "INSERT", "PARALLEL" }, 0);

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

    for (int i = 0; i < 32; ++i) {
        layout.push_back(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID{ stepExtraFxParamId(i), 2 },
            juce::String::formatted("Step %02d Extra FX Mask", i + 1), 0, 31, 31));
    }
    addFxParam(kChorusAmt, kChorusOn, "Chorus", 0.0f, 2);
    addFxParam(kFlangerAmt, kFlangerOn, "Flanger", 0.0f, 2);
    addFxParam(kPhaserAmt, kPhaserOn, "Phaser", 0.0f, 2);
    addFxParam(kPingPongAmt, kPingPongOn, "Ping-Pong", 0.0f, 2);
    addFxParam(kSaturationAmt, kSaturationOn, "Saturation", 0.0f, 2);

    return { layout.begin(), layout.end() };
}

} // namespace openglitch::params
