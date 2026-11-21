#pragma once

#include "../Core/GlitchProcessorCore.h"
#include "../suite/AbCompare.h"
#include "../suite/ParameterUndo.h"
#include "../suite/PresetLibrary.h"
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
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    openglitch::suite::ParameterUndo& getUndo() { return undo_; }
    openglitch::suite::PresetLibrary& getPresets() { return presets_; }
    void loadFactoryProgram(int index);
    void loadUserPreset(const juce::String& name);
    bool saveUserPreset(const juce::String& name, juce::String& error);
    bool renameUserPreset(const juce::String& from, const juce::String& to, juce::String& error);
    bool deleteUserPreset(const juce::String& name, juce::String& error);
    void selectAbSlot(bool slotA);
    void copyAbAtoB();
    void copyAbBtoA();
    bool abIsA() const { return ab_.isA(); }
    juce::String currentPresetLabel() const { return presetLabel_; }
    bool currentPresetIsFactory() const { return presetIsFactory_; }
    float getEditorScale() const { return editorScale_.load(); }
    void setEditorScale(float scale) { editorScale_.store(scale); }

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    float getVisualBeat() const { return visualBeat_.load(std::memory_order_relaxed); }
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
    void applyFactory(int index);
    void resetEnvelopes();
    juce::ValueTree musicalState();
    juce::ValueTree captureExtra() const;
    void applyExtra(const juce::ValueTree& extra);
    void restoreMusicalState(const juce::ValueTree& state);

    openglitch::suite::ParameterUndo undo_;
    openglitch::suite::AbCompare ab_;
    openglitch::suite::PresetLibrary presets_;
    int currentProgram_ = 0;
    bool programApplied_ = false;
    bool presetIsFactory_ = true;
    juce::String presetLabel_;

    openglitch::core::GlitchProcessorCore core_;
    std::atomic<float> visualBeat_{ 0.0f };
    std::atomic<int> stateVersion_{ 0 };
    std::atomic<float> editorScale_{ 1.0f };
    std::array<std::atomic<float>*, 32> stepParams_{};
    std::array<std::atomic<float>*, 32> stepFxParams_{};
    std::array<std::atomic<float>*, 32> stepExtraFxParams_{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenGlitchFXProcessor)
};
