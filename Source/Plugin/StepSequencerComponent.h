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
    void resetAllStepFxMasksToAll();

    // Paint target mode: -1 = All FX (default), 0..9 = specific Glitch FX index
    void setPaintTargetFx(int fxIndex);
    int getPaintTargetFx() const { return paintTargetFx_; }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;

private:
    int getStepAtPosition(juce::Point<float> pos) const;
    juce::Rectangle<float> getStepBounds(int stepIndex) const;

    void setStepParam(int stepIndex, bool state);
    bool getStepParam(int stepIndex) const;

    void setStepFxMaskParam(int stepIndex, uint16_t mask);
    uint16_t getStepFxMaskParam(int stepIndex) const;

    void ensureFxAudible(int fxIndex);
    void applyPaintBrushToStep(int stepIndex, bool isInitialClick);
    void showStepContextMenu(int stepIndex);
    static juce::String formatStepFxBadge(uint16_t mask);

    juce::AudioProcessorValueTreeState& apvts_;
    const GlitchLookAndFeel& lnf_;

    int currentStep_ = 0;
    float barPhase_ = 0.0f;
    bool isStepActive_ = false;
    float gateEnvelope_ = 0.0f;

    int paintTargetFx_ = -1; // -1 = ALL FX, 0..9 = specific FX brush
    bool dragTargetState_ = true;
};

} // namespace openglitch::ui
