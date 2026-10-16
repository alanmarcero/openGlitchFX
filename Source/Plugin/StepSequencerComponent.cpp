#include "StepSequencerComponent.h"
#include <algorithm>

namespace openglitch::ui {

StepSequencerComponent::StepSequencerComponent(juce::AudioProcessorValueTreeState& apvts,
                                               const GlitchLookAndFeel& lnf)
    : apvts_(apvts), lnf_(lnf) {
}

void StepSequencerComponent::setPlayheadState(int currentStep, float barPhase, bool isStepActive, float gateEnvelope) {
    if (currentStep_ != currentStep ||
        std::abs(barPhase_ - barPhase) > 0.004f ||
        isStepActive_ != isStepActive ||
        std::abs(gateEnvelope_ - gateEnvelope) > 0.02f) {
        currentStep_ = std::clamp(currentStep, 0, 31);
        barPhase_ = std::clamp(barPhase, 0.0f, 1.0f);
        isStepActive_ = isStepActive;
        gateEnvelope_ = std::clamp(gateEnvelope, 0.0f, 1.0f);
        repaint();
    }
}

bool StepSequencerComponent::getStepParam(int stepIndex) const {
    if (stepIndex < 0 || stepIndex >= 32) return false;
    if (auto* raw = apvts_.getRawParameterValue(openglitch::params::stepParamId(stepIndex))) {
        return raw->load() > 0.5f;
    }
    return false;
}

void StepSequencerComponent::setStepParam(int stepIndex, bool state) {
    if (stepIndex < 0 || stepIndex >= 32) return;
    if (auto* param = apvts_.getParameter(openglitch::params::stepParamId(stepIndex))) {
        param->beginChangeGesture();
        param->setValueNotifyingHost(state ? 1.0f : 0.0f);
        param->endChangeGesture();
    }
}

void StepSequencerComponent::applyPreset(openglitch::core::StepPatternPreset preset) {
    openglitch::core::BeatStepSequencer tempSeq;
    tempSeq.applyPreset(preset);
    for (int i = 0; i < 32; ++i) {
        setStepParam(i, tempSeq.getStep(i));
    }
    repaint();
}

void StepSequencerComponent::invertPattern() {
    for (int i = 0; i < 32; ++i) {
        setStepParam(i, !getStepParam(i));
    }
    repaint();
}

juce::Rectangle<float> StepSequencerComponent::getStepBounds(int stepIndex) const {
    const auto bounds = getLocalBounds().toFloat().reduced(10.0f, 6.0f);
    const float topHeaderH = 22.0f;
    const float bottomBarH = 10.0f;
    const auto gridArea = bounds.withTrimmedTop(topHeaderH).withTrimmedBottom(bottomBarH);

    const int beatGroup = stepIndex / 8; // 0..3
    const float beatGap = 8.0f;
    const float stepGap = 3.0f;
    const float totalGaps = 3.0f * beatGap + 28.0f * stepGap;
    const float stepW = (gridArea.getWidth() - totalGaps) / 32.0f;

    const float x = gridArea.getX()
                  + static_cast<float>(stepIndex) * (stepW + stepGap)
                  + static_cast<float>(beatGroup) * (beatGap - stepGap);

    return { x, gridArea.getY(), stepW, gridArea.getHeight() };
}

int StepSequencerComponent::getStepAtPosition(juce::Point<float> pos) const {
    for (int i = 0; i < 32; ++i) {
        if (getStepBounds(i).expanded(1.5f, 2.0f).contains(pos)) {
            return i;
        }
    }
    return -1;
}

void StepSequencerComponent::mouseDown(const juce::MouseEvent& event) {
    const int step = getStepAtPosition(event.position);
    if (step >= 0) {
        dragTargetState_ = !getStepParam(step);
        setStepParam(step, dragTargetState_);
        repaint();
    }
}

void StepSequencerComponent::mouseDrag(const juce::MouseEvent& event) {
    const int step = getStepAtPosition(event.position);
    if (step >= 0) {
        if (getStepParam(step) != dragTargetState_) {
            setStepParam(step, dragTargetState_);
            repaint();
        }
    }
}

void StepSequencerComponent::paint(juce::Graphics& g) {
    const auto& pal = lnf_.getPalette();
    const auto fullBounds = getLocalBounds().toFloat();

    // Background Card
    g.setColour(pal.cardBg);
    g.fillRoundedRectangle(fullBounds, 6.0f);
    g.setColour(pal.cardBorder);
    g.drawRoundedRectangle(fullBounds, 6.0f, 1.2f);

    // Beat Group Headers (BEAT 1..4)
    const char* beatTitles[4] = {
        "BEAT 1  (STEPS 1-8)",
        "BEAT 2  (STEPS 9-16)",
        "BEAT 3  (STEPS 17-24)",
        "BEAT 4  (STEPS 25-32)"
    };

    for (int b = 0; b < 4; ++b) {
        const auto firstStepRect = getStepBounds(b * 8);
        const auto lastStepRect  = getStepBounds(b * 8 + 7);
        juce::Rectangle<float> beatHeader(firstStepRect.getX(), fullBounds.getY() + 5.0f,
                                          lastStepRect.getRight() - firstStepRect.getX(), 18.0f);

        const bool playheadInBeat = (currentStep_ / 8) == b;
        g.setColour(playheadInBeat ? pal.accentPrimary.withAlpha(0.18f) : pal.headerPlate.withAlpha(0.7f));
        g.fillRoundedRectangle(beatHeader, 3.0f);

        g.setColour(playheadInBeat ? pal.accentPrimary : pal.textMuted);
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(beatTitles[b], beatHeader.toNearestInt(), juce::Justification::centred);
    }

    // Draw all 32 steps
    for (int i = 0; i < 32; ++i) {
        const auto r = getStepBounds(i);
        const bool enabled = getStepParam(i);
        const bool isPlayhead = (i == currentStep_);
        const bool isQuarterStart = (i % 8 == 0);
        const bool isSixteenthStart = (i % 2 == 0);

        if (enabled) {
            juce::Colour topCol = (i >= 24) ? pal.accentSecondary : pal.stepActiveFill;
            if (isPlayhead) {
                topCol = topCol.brighter(0.45f);
            }
            juce::ColourGradient padGrad(topCol, r.getCentreX(), r.getY(),
                                         topCol.darker(0.45f), r.getCentreX(), r.getBottom(), false);
            g.setGradientFill(padGrad);
            g.fillRoundedRectangle(r, 3.5f);

            // Outer glow on active playhead step
            if (isPlayhead) {
                g.setColour(topCol.withAlpha(0.45f));
                g.drawRoundedRectangle(r.expanded(2.0f), 4.5f, 2.5f);
            }
        } else {
            juce::Colour offCol = isQuarterStart ? pal.stepInactiveFill.brighter(0.15f)
                                                 : pal.stepInactiveFill;
            g.setColour(offCol);
            g.fillRoundedRectangle(r, 3.5f);
        }

        // Border
        if (isPlayhead) {
            g.setColour(pal.stepPlayheadOutline);
            g.drawRoundedRectangle(r, 3.5f, 2.0f);
        } else {
            g.setColour(enabled ? pal.textBright.withAlpha(0.55f) : pal.cardBorder);
            g.drawRoundedRectangle(r, 3.5f, 1.0f);
        }

        // Step number label (1..32)
        g.setColour(enabled ? juce::Colours::black.withAlpha(0.88f)
                            : (isSixteenthStart ? pal.textBright.withAlpha(0.75f) : pal.textMuted.withAlpha(0.55f)));
        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        g.drawText(juce::String(i + 1), r.withTrimmedBottom(r.getHeight() * 0.35f).toNearestInt(),
                   juce::Justification::centred);

        // Bottom LED indicator bar inside step pad
        auto ledBar = r.withTop(r.getBottom() - 6.0f).reduced(3.0f, 1.5f);
        g.setColour(enabled ? pal.textBright.withAlpha(0.9f) : juce::Colours::black.withAlpha(0.4f));
        g.fillRoundedRectangle(ledBar, 1.5f);
    }

    // Continuous 4/4 Bar Progress Ribbon at bottom of sequencer panel
    const auto firstRect = getStepBounds(0);
    const auto lastRect  = getStepBounds(31);
    juce::Rectangle<float> progTrack(firstRect.getX(), firstRect.getBottom() + 5.0f,
                                     lastRect.getRight() - firstRect.getX(), 4.0f);
    g.setColour(pal.stepInactiveFill);
    g.fillRoundedRectangle(progTrack, 2.0f);

    juce::Rectangle<float> progFill = progTrack.withWidth(progTrack.getWidth() * barPhase_);
    g.setColour(isStepActive_ ? pal.accentPrimary : pal.textMuted.withAlpha(0.5f));
    g.fillRoundedRectangle(progFill, 2.0f);
}

} // namespace openglitch::ui
