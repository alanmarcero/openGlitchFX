#include "PluginProcessor.h"
#include "PluginEditor.h"

OpenGlitchFXProcessor::OpenGlitchFXProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, juce::Identifier("OpenGlitchFXParameters"), openglitch::params::createParameterLayout()) {
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

    // 1. 32-Step Beat Sequencer Pattern
    for (int i = 0; i < 32; ++i) {
        if (auto* raw = parameters.getRawParameterValue(stepParamId(i))) {
            core_.setStep(i, raw->load() > 0.5f);
        }
    }

    // 2. Gate smoothing
    const float smoothMs = *parameters.getRawParameterValue(kSmoothMs);
    core_.setGateSmoothingMs(smoothMs);

    // 3. All 10 Glitch FX (Phase 1 + Phase 2)
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

    // 4. Master
    core_.setMasterMix(*parameters.getRawParameterValue(kMasterMix) * 0.01f);
    core_.setMasterGainDb(*parameters.getRawParameterValue(kMasterGain));
    core_.setMasterBypassed(*parameters.getRawParameterValue(kMasterPower) < 0.5f);
}

void OpenGlitchFXProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midiMessages*/) {
    juce::ScopedNoDenormals noDenormals;

    syncParametersToCore();

    // Query Host Playhead for Beat Sync
    const bool useHostSync = *parameters.getRawParameterValue(openglitch::params::kHostSync) > 0.5f;
    double bpm = static_cast<double>(*parameters.getRawParameterValue(openglitch::params::kInternalBpm));
    bool hasValidHostPpq = false;
    double hostPpq = 0.0;

    if (auto* playHead = getPlayHead()) {
        if (const auto pos = playHead->getPosition()) {
            if (const auto hostBpm = pos->getBpm()) {
                if (*hostBpm > 10.0) {
                    bpm = *hostBpm;
                }
            }
            if (const auto ppq = pos->getPpqPosition()) {
                hostPpq = *ppq;
                hasValidHostPpq = true;
            }
        }
    }

    core_.setTransport(bpm, useHostSync, hasValidHostPpq, hostPpq);

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
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
            syncParametersToCore();
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new OpenGlitchFXProcessor();
}
