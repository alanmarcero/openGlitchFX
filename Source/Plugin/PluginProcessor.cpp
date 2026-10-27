#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace {

const juce::Identifier kEnvelopesNode{ "ENVELOPES" };

juce::Identifier envelopePropertyId(openglitch::core::EnvTarget target) {
    return juce::Identifier("env" + juce::String(static_cast<int>(target)));
}

juce::String encodePoints(const std::vector<openglitch::core::EnvelopePoint>& points) {
    juce::StringArray pairs;
    for (const auto& p : points) {
        pairs.add(juce::String(p.x, 4) + "," + juce::String(p.y, 4));
    }
    return pairs.joinIntoString(";");
}

std::vector<openglitch::core::EnvelopePoint> decodePoints(const juce::String& text) {
    std::vector<openglitch::core::EnvelopePoint> points;
    for (const auto& pair : juce::StringArray::fromTokens(text, ";", {})) {
        const auto x = pair.upToFirstOccurrenceOf(",", false, false).getFloatValue();
        const auto y = pair.fromFirstOccurrenceOf(",", false, false).getFloatValue();
        points.push_back({ std::clamp(x, 0.0f, 1.0f), std::clamp(y, 0.0f, 1.0f) });
    }
    return points;
}

} // namespace

OpenGlitchFXProcessor::OpenGlitchFXProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, juce::Identifier("OpenGlitchFXParameters"), openglitch::params::createParameterLayout()) {
    using openglitch::core::EnvTarget;
    for (int env = 0; env < static_cast<int>(EnvTarget::Count); ++env) {
        const auto target = static_cast<EnvTarget>(env);
        setEnvelopePoints(target, openglitch::core::ModEnvelope::defaultPoints(target));
    }
}

std::vector<openglitch::core::EnvelopePoint> OpenGlitchFXProcessor::getEnvelopePoints(openglitch::core::EnvTarget target) const {
    const auto node = parameters.state.getChildWithName(kEnvelopesNode);
    const auto points = decodePoints(node.getProperty(envelopePropertyId(target)).toString());
    return points.empty() ? openglitch::core::ModEnvelope::defaultPoints(target) : points;
}

void OpenGlitchFXProcessor::setEnvelopePoints(openglitch::core::EnvTarget target,
                                              const std::vector<openglitch::core::EnvelopePoint>& points) {
    auto node = parameters.state.getOrCreateChildWithName(kEnvelopesNode, nullptr);
    node.setProperty(envelopePropertyId(target), encodePoints(points), nullptr);
    core_.setEnvelopePoints(target, points);
}

void OpenGlitchFXProcessor::syncEnvelopePointsToCore() {
    using openglitch::core::EnvTarget;
    for (int env = 0; env < static_cast<int>(EnvTarget::Count); ++env) {
        const auto target = static_cast<EnvTarget>(env);
        core_.setEnvelopePoints(target, getEnvelopePoints(target));
    }
}

void OpenGlitchFXProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/) {
    core_.prepare(sampleRate);
    syncParametersToCore();
}

void OpenGlitchFXProcessor::releaseResources() {
    core_.reset();
}

bool OpenGlitchFXProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& mainIn = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != juce::AudioChannelSet::mono() && mainIn != juce::AudioChannelSet::stereo()) {
        return false;
    }

    return mainIn == mainOut;
}

void OpenGlitchFXProcessor::syncParametersToCore() {
    using namespace openglitch::params;
    using openglitch::core::GlitchFxId;

    // 1. 32-Step Beat Sequencer Pattern + Per-Pad 15-FX Routing Bitmask
    for (int i = 0; i < 32; ++i) {
        if (auto* raw = parameters.getRawParameterValue(stepParamId(i))) {
            core_.setStep(i, raw->load() > 0.5f);
        }
        core_.setStepFxMask(i, readStepFxMask(parameters, i));
    }

    // 2. Gate smoothing
    const float smoothMs = *parameters.getRawParameterValue(kSmoothMs);
    core_.setGateSmoothingMs(smoothMs);

    // 3. All 15 Glitch FX
    auto syncFx = [&](GlitchFxId id, const char* amtId, const char* onId) {
        const float amt = *parameters.getRawParameterValue(amtId) * 0.01f;
        const bool on = *parameters.getRawParameterValue(onId) > 0.5f;
        core_.setEffectAmount(id, amt);
        core_.setEffectEnabled(id, on);
    };

    // Row 1 (Phase 1: 5 FX)
    syncFx(GlitchFxId::Bitcrush,    kBitcrushAmt,    kBitcrushOn);
    syncFx(GlitchFxId::Downsample,  kDownsampleAmt,  kDownsampleOn);
    syncFx(GlitchFxId::Stutter,     kStutterAmt,     kStutterOn);
    syncFx(GlitchFxId::TapeStop,    kTapeStopAmt,    kTapeStopOn);
    syncFx(GlitchFxId::RingMod,     kRingModAmt,     kRingModOn);

    // Row 2 (Phase 2: 5 Additional FX -> 10 Total)
    syncFx(GlitchFxId::Reverse,     kReverseAmt,     kReverseOn);
    syncFx(GlitchFxId::Chopper,     kChopperAmt,     kChopperOn);
    syncFx(GlitchFxId::PitchGlitch, kPitchGlitchAmt, kPitchGlitchOn);
    syncFx(GlitchFxId::CombReso,    kCombResoAmt,    kCombResoOn);
    syncFx(GlitchFxId::Wavefold,    kWavefoldAmt,    kWavefoldOn);

    syncFx(GlitchFxId::Chorus, kChorusAmt, kChorusOn);
    syncFx(GlitchFxId::Flanger, kFlangerAmt, kFlangerOn);
    syncFx(GlitchFxId::Phaser, kPhaserAmt, kPhaserOn);
    syncFx(GlitchFxId::PingPong, kPingPongAmt, kPingPongOn);
    syncFx(GlitchFxId::Saturation, kSaturationAmt, kSaturationOn);

    // 4. Buffer, tape, filter, envelopes, trigger
    using namespace openglitch::core;
    auto choice = [&](const char* id) { return static_cast<int>(std::round(parameters.getRawParameterValue(id)->load())); };
    auto flag = [&](const char* id) { return parameters.getRawParameterValue(id)->load() > 0.5f; };
    auto value = [&](const char* id) { return parameters.getRawParameterValue(id)->load(); };

    StutterSettings stutter;
    stutter.syncedSliceBeats = kStutterSliceBeats[static_cast<size_t>(std::clamp(choice(kStutterSync), 0, 5))];
    stutter.advance = flag(kStutterAdvance);
    stutter.reverse = static_cast<StutterReverse>(choice(kStutterReverse));
    stutter.gate = value(kStutterGate) * 0.01f;
    stutter.pitchStepSemitones = value(kStutterPitch);
    core_.setStutterSettings(stutter);

    TapeStopSettings tape;
    tape.direction = static_cast<TapeDirection>(choice(kTapeDirection));
    tape.stepped = choice(kTapeCurve) == 1;
    tape.slip = flag(kTapeSlip);
    tape.lowpass = flag(kTapeLowpass);
    tape.fade = flag(kTapeFade);
    core_.setTapeStopSettings(tape, kNoteLengthBeats[static_cast<size_t>(std::clamp(choice(kTapeLength), 0, 6))]);

    core_.setFilter(static_cast<FilterMode>(choice(kFilterMode)), value(kFilterCutoff), value(kFilterReso) * 0.01f);

    for (size_t env = 0; env < kEnvDescriptors.size(); ++env) {
        const auto& desc = kEnvDescriptors[env];
        const auto lengthBeats = kNoteLengthBeats[static_cast<size_t>(std::clamp(choice(desc.rateParamId), 0, 6))];
        core_.setEnvelopeSettings(static_cast<EnvTarget>(env), { flag(desc.onParamId), value(desc.depthParamId) * 0.01f, lengthBeats });
    }

    core_.setTriggerMode(static_cast<TriggerMode>(choice(kTriggerMode)), choice(kTriggerStart) == 1);
    core_.setMixMode(static_cast<MixMode>(choice(kMixMode)));

    // 5. Master
    core_.setMasterMix(*parameters.getRawParameterValue(kMasterMix) * 0.01f);
    core_.setMasterGainDb(*parameters.getRawParameterValue(kMasterGain));
    core_.setMasterBypassed(*parameters.getRawParameterValue(kMasterPower) < 0.5f);
}

void OpenGlitchFXProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    syncParametersToCore();

    // Query Host Playhead for Beat Sync
    const bool useHostSync = *parameters.getRawParameterValue(openglitch::params::kHostSync) > 0.5f;
    double bpm = static_cast<double>(*parameters.getRawParameterValue(openglitch::params::kInternalBpm));
    bool hasValidHostPpq = false;
    double hostPpq = 0.0;

    if (auto* playHead = getPlayHead()) {
        if (const auto pos = playHead->getPosition()) {
            if (const auto hostBpm = pos->getBpm(); useHostSync && hostBpm) {
                if (*hostBpm > 10.0) {
                    bpm = *hostBpm;
                }
            }
            // Hosts keep reporting a frozen PPQ while stopped. Use it only while playing, so the internal clock runs otherwise.
            if (const auto ppq = pos->getPpqPosition(); ppq && pos->getIsPlaying()) {
                hostPpq = *ppq;
                hasValidHostPpq = true;
            }
        }
    }

    core_.setTransport(bpm, useHostSync, hasValidHostPpq, hostPpq);

    for (const auto metadata : midiMessages) {
        const auto message = metadata.getMessage();
        if (message.isNoteOnOrOff()) {
            core_.queueMidiNote(metadata.samplePosition, message.getNoteNumber(), message.isNoteOn());
        }
    }

    const int totalNumInputChannels = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int ch = totalNumInputChannels; ch < totalNumOutputChannels; ++ch) {
        buffer.clear(ch, 0, numSamples);
    }

    if (numSamples == 0 || totalNumInputChannels == 0) {
        return;
    }

    float* leftChannel = buffer.getWritePointer(0);
    float* rightChannel = (totalNumInputChannels > 1) ? buffer.getWritePointer(1) : nullptr;

    core_.processBlock(leftChannel, rightChannel, numSamples);
}

double OpenGlitchFXProcessor::getTailLengthSeconds() const {
    const bool echoEnabled = parameters.getRawParameterValue(openglitch::params::kPingPongOn)->load() > 0.5f;
    const float amount = parameters.getRawParameterValue(openglitch::params::kPingPongAmt)->load();
    return echoEnabled && amount > 0.01f ? 40.0 : 0.0;
}

juce::AudioProcessorEditor* OpenGlitchFXProcessor::createEditor() {
    return new OpenGlitchFXEditor(*this);
}

void OpenGlitchFXProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (auto xmlState = parameters.copyState().createXml()) {
        copyXmlToBinary(*xmlState, destData);
    }
}

void OpenGlitchFXProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xmlState = getXmlFromBinary(data, sizeInBytes)) {
        if (xmlState->hasTagName(parameters.state.getType())) {
            auto restored = juce::ValueTree::fromXml(*xmlState);
            // Missing v1.2 parameters must not retain values from the previously loaded session.
            for (auto* parameter : getParameters()) {
                auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
                if (ranged != nullptr && ranged->getVersionHint() == 2
                    && !restored.getChildWithProperty("id", ranged->paramID).isValid()) {
                    juce::ValueTree value("PARAM");
                    value.setProperty("id", ranged->paramID, nullptr);
                    const float fallback = ranged->paramID.startsWith("step_fx_extra_") ? 0.0f
                        : ranged->convertFrom0to1(ranged->getDefaultValue());
                    value.setProperty("value", fallback, nullptr);
                    restored.appendChild(value, nullptr);
                }
            }
            parameters.replaceState(restored);
            syncParametersToCore();
            syncEnvelopePointsToCore();
            ++stateVersion_;
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new OpenGlitchFXProcessor();
}
