#pragma once

#include "../Core/GlitchProcessorCore.h"
#include "Parameters.h"
#include <juce_audio_processors/juce_audio_processors.h>

class OpenGlitchFXProcessor final : public juce::AudioProcessor {
public:
    OpenGlitchFXProcessor();
    ~OpenGlitchFXProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "openGlitchFX"; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int /*index*/) override {}
    const juce::String getProgramName(int /*index*/) override { return {}; }
    void changeProgramName(int /*index*/, const juce::String& /*newName*/) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    openglitch::core::MeterTelemetry getTelemetry() const { return core_.getTelemetry(); }

    // The session saves envelope points in the state tree.
    std::vector<openglitch::core::EnvelopePoint> getEnvelopePoints(openglitch::core::EnvTarget target) const;
    void setEnvelopePoints(openglitch::core::EnvTarget target, const std::vector<openglitch::core::EnvelopePoint>& points);

    // The editor reloads envelope points when this changes.
    int getStateVersion() const { return stateVersion_.load(); }

    juce::AudioProcessorValueTreeState parameters;

private:
    void syncParametersToCore();
    void syncEnvelopePointsToCore();

    openglitch::core::GlitchProcessorCore core_;
    std::atomic<int> stateVersion_{ 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenGlitchFXProcessor)
};
