#pragma once

#include "GlitchLookAndFeel.h"
#include "Parameters.h"
#include "../Core/BeatStepSequencer.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace openglitch::ui {

class StepSequencerComponent final : public juce::Component {
public:
    StepSequencerComponent(juce::AudioProcessorValueTreeState& apvts,
                           const GlitchLookAndFeel& lnf);

    void setPlayheadState(int currentStep, float barPhase, bool isStepActive, float gateEnvelope);
    void applyPreset(openglitch::core::StepPatternPreset preset);
    void invertPattern();

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;

private:
    int getStepAtPosition(juce::Point<float> pos) const;
    juce::Rectangle<float> getStepBounds(int stepIndex) const;
    void setStepParam(int stepIndex, bool state);
    bool getStepParam(int stepIndex) const;

    juce::AudioProcessorValueTreeState& apvts_;
    const GlitchLookAndFeel& lnf_;

    int currentStep_ = 0;
    float barPhase_ = 0.0f;
    bool isStepActive_ = false;
    float gateEnvelope_ = 0.0f;
    bool dragTargetState_ = true;
};

} // namespace openglitch::ui
