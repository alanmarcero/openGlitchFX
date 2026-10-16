#include "StepSequencerComponent.h"
#include <algorithm>
#include <bit>

namespace openglitch::ui {

namespace {
constexpr uint16_t kAllFx = openglitch::core::BeatStepSequencer::kAllFxMask;
} // namespace

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

void StepSequencerComponent::setPaintTargetFx(int fxIndex) {
    paintTargetFx_ = std::clamp(fxIndex, -1, 9);
    repaint();
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

uint16_t StepSequencerComponent::getStepFxMaskParam(int stepIndex) const {
    if (stepIndex < 0 || stepIndex >= 32) return kAllFx;
    if (auto* raw = apvts_.getRawParameterValue(openglitch::params::stepFxParamId(stepIndex))) {
        const int val = static_cast<int>( std::round(raw->load()) );
        return static_cast<uint16_t>(std::clamp(val, 0, static_cast<int>(kAllFx))) & kAllFx;
    }
    return kAllFx;
}

void StepSequencerComponent::setStepFxMaskParam(int stepIndex, uint16_t mask) {
    if (stepIndex < 0 || stepIndex >= 32) return;
    const uint16_t clamped = mask & kAllFx;
    if (auto* param = apvts_.getParameter(openglitch::params::stepFxParamId(stepIndex))) {
        const float norm = param->convertTo0to1(static_cast<float>(clamped));
        param->beginChangeGesture();
        param->setValueNotifyingHost(norm);
        param->endChangeGesture();
    }
}

void StepSequencerComponent::ensureFxAudible(int fxIndex) {
    if (fxIndex < 0 || fxIndex >= 10) return;
    const auto& desc = openglitch::params::kFxDescriptors[static_cast<size_t>(fxIndex)];

    // Ensure module ON toggle is enabled
    if (auto* onParam = apvts_.getParameter(desc.onParamId)) {
        if (onParam->getValue() < 0.5f) {
            onParam->beginChangeGesture();
            onParam->setValueNotifyingHost(1.0f);
            onParam->endChangeGesture();
        }
    }

    // If the effect's amount knob is currently at 0%, raise it to a musical 50% default
    if (auto* amtRaw = apvts_.getRawParameterValue(desc.amtParamId)) {
        if (amtRaw->load() < 1.0f) {
            if (auto* amtParam = apvts_.getParameter(desc.amtParamId)) {
                amtParam->beginChangeGesture();
                amtParam->setValueNotifyingHost(amtParam->convertTo0to1(50.0f));
                amtParam->endChangeGesture();
            }
        }
    }
}

void StepSequencerComponent::applyPreset(openglitch::core::StepPatternPreset preset) {
    openglitch::core::BeatStepSequencer tempSeq;
    tempSeq.applyPreset(preset);
    for (int i = 0; i < 32; ++i) {
        const bool stepOn = tempSeq.getStep(i);
        setStepParam(i, stepOn);
        if (stepOn && paintTargetFx_ >= 0 && paintTargetFx_ < 10) {
            setStepFxMaskParam(i, static_cast<uint16_t>(1u << paintTargetFx_));
            ensureFxAudible(paintTargetFx_);
        } else if (stepOn && getStepFxMaskParam(i) == 0u) {
            setStepFxMaskParam(i, kAllFx);
        }
    }
    repaint();
}

void StepSequencerComponent::invertPattern() {
    for (int i = 0; i < 32; ++i) {
        setStepParam(i, !getStepParam(i));
    }
    repaint();
}

void StepSequencerComponent::resetAllStepFxMasksToAll() {
    for (int i = 0; i < 32; ++i) {
        setStepFxMaskParam(i, kAllFx);
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

void StepSequencerComponent::applyPaintBrushToStep(int step, bool isInitialClick) {
    if (step < 0 || step >= 32) return;

    if (paintTargetFx_ < 0) {
        // Default mode: toggle entire pad ON/OFF
        if (isInitialClick) {
            dragTargetState_ = !getStepParam(step);
        }
        setStepParam(step, dragTargetState_);
        if (dragTargetState_ && getStepFxMaskParam(step) == 0u) {
            setStepFxMaskParam(step, kAllFx);
        }
    } else {
        // Specific FX Brush mode (0..9): paint or toggle that specific FX on this pad
        const uint16_t fxBit = static_cast<uint16_t>(1u << paintTargetFx_);
        const bool stepWasOn = getStepParam(step);
        const uint16_t oldMask = getStepFxMaskParam(step);

        if (isInitialClick) {
            // If pad was off, or had ALL FX, or didn't have this FX bit -> we are painting ON
            if (!stepWasOn || oldMask == kAllFx || (oldMask & fxBit) == 0u) {
                dragTargetState_ = true;
            } else {
                dragTargetState_ = false;
            }
        }

        if (dragTargetState_) {
            ensureFxAudible(paintTargetFx_);
            if (!stepWasOn || oldMask == kAllFx) {
                // Solo-assign this painted FX to the pad
                setStepFxMaskParam(step, fxBit);
            } else {
                // Add this FX to existing custom FX on the pad
                setStepFxMaskParam(step, static_cast<uint16_t>(oldMask | fxBit));
            }
            setStepParam(step, true);
        } else {
            const uint16_t newMask = static_cast<uint16_t>(oldMask & ~fxBit);
            if (newMask == 0u) {
                setStepParam(step, false);
                setStepFxMaskParam(step, kAllFx);
            } else {
                setStepFxMaskParam(step, newMask);
            }
        }
    }
    repaint();
}

void StepSequencerComponent::showStepContextMenu(int step) {
    if (step < 0 || step >= 32) return;

    const bool stepOn = getStepParam(step);
    const uint16_t mask = getStepFxMaskParam(step);

    juce::PopupMenu menu;
    menu.addSectionHeader(juce::String::formatted("PAD %02d/32 — EFFECT ROUTING", step + 1));

    menu.addItem(1, "Enable ALL 10 FX on Pad", true, stepOn && mask == kAllFx);
    menu.addItem(2, "Disable Pad (Dry Pass-Through)", true, !stepOn);

    menu.addSeparator();
    menu.addSectionHeader("SOLO SINGLE EFFECT ON THIS PAD");
    for (int fx = 0; fx < 10; ++fx) {
        const auto& desc = openglitch::params::kFxDescriptors[static_cast<size_t>(fx)];
        const uint16_t singleBit = static_cast<uint16_t>(1u << fx);
        const bool isSoloed = stepOn && (mask == singleBit);
        menu.addItem(100 + fx,
                     juce::String::formatted("Only %s (%s)", desc.name, desc.shortCode),
                     true,
                     isSoloed);
    }

    menu.addSeparator();
    menu.addSectionHeader("TOGGLE MULTI-FX ON THIS PAD");
    for (int fx = 0; fx < 10; ++fx) {
        const auto& desc = openglitch::params::kFxDescriptors[static_cast<size_t>(fx)];
        const uint16_t bit = static_cast<uint16_t>(1u << fx);
        const bool fxChecked = stepOn && ((mask & bit) != 0u);
        menu.addItem(200 + fx,
                     juce::String::formatted("%d. %s [%s]", fx + 1, desc.name, desc.shortCode),
                     true,
                     fxChecked);
    }

    menu.addSeparator();
    menu.addItem(300, "Copy Pad FX Routing to Current Beat (8 Pads)");
    menu.addItem(301, "Copy Pad FX Routing to All 32 Pads");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                       [this, step, stepOn, mask](int result) {
        if (result == 0) return;

        if (result == 1) {
            setStepFxMaskParam(step, kAllFx);
            setStepParam(step, true);
        } else if (result == 2) {
            setStepParam(step, false);
        } else if (result >= 100 && result < 110) {
            const int fx = result - 100;
            ensureFxAudible(fx);
            setStepFxMaskParam(step, static_cast<uint16_t>(1u << fx));
            setStepParam(step, true);
        } else if (result >= 200 && result < 210) {
            const int fx = result - 200;
            const uint16_t bit = static_cast<uint16_t>(1u << fx);
            uint16_t baseMask = stepOn ? mask : 0u;
            if (baseMask == kAllFx) {
                // Unchecking one from ALL FX leaves the other 9 checked
                baseMask = static_cast<uint16_t>(kAllFx & ~bit);
                setStepFxMaskParam(step, baseMask);
                setStepParam(step, true);
            } else {
                const bool turningOn = (baseMask & bit) == 0u;
                if (turningOn) {
                    ensureFxAudible(fx);
                    baseMask |= bit;
                    setStepFxMaskParam(step, baseMask);
                    setStepParam(step, true);
                } else {
                    baseMask &= static_cast<uint16_t>(~bit);
                    if (baseMask == 0u) {
                        setStepParam(step, false);
                        setStepFxMaskParam(step, kAllFx);
                    } else {
                        setStepFxMaskParam(step, baseMask);
                        setStepParam(step, true);
                    }
                }
            }
        } else if (result == 300) {
            const int beatStart = (step / 8) * 8;
            for (int i = beatStart; i < beatStart + 8; ++i) {
                setStepFxMaskParam(i, mask);
                setStepParam(i, stepOn);
            }
        } else if (result == 301) {
            for (int i = 0; i < 32; ++i) {
                setStepFxMaskParam(i, mask);
            }
        }
        repaint();
    });
}

void StepSequencerComponent::mouseDown(const juce::MouseEvent& event) {
    const int step = getStepAtPosition(event.position);
    if (step < 0) return;

    if (event.mods.isPopupMenu()) {
        showStepContextMenu(step);
        return;
    }

    applyPaintBrushToStep(step, true);
}

void StepSequencerComponent::mouseDrag(const juce::MouseEvent& event) {
    if (event.mods.isPopupMenu()) return;

    const int step = getStepAtPosition(event.position);
    if (step >= 0) {
        applyPaintBrushToStep(step, false);
    }
}

juce::String StepSequencerComponent::formatStepFxBadge(uint16_t mask) {
    mask &= kAllFx;
    if (mask == 0u) return "OFF";
    if (mask == kAllFx) return "ALL";

    const int count = std::popcount(mask);
    if (count == 1) {
        for (int fx = 0; fx < 10; ++fx) {
            if ((mask & (1u << fx)) != 0u) {
                return openglitch::params::kFxDescriptors[static_cast<size_t>(fx)].shortCode;
            }
        }
    }
    return juce::String(count) + "FX";
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
        "BEAT 1 (1-8)  [R-CLICK PAD FOR FX]",
        "BEAT 2 (9-16)",
        "BEAT 3 (17-24)",
        "BEAT 4 (25-32)"
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
        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        g.drawText(beatTitles[b], beatHeader.toNearestInt(), juce::Justification::centred);
    }

    // Draw all 32 steps
    for (int i = 0; i < 32; ++i) {
        const auto r = getStepBounds(i);
        const uint16_t fxMask = getStepFxMaskParam(i);
        const bool enabled = getStepParam(i) && (fxMask != 0u);
        const bool isCustomMask = enabled && (fxMask != kAllFx);
        const bool isPlayhead = (i == currentStep_);
        const bool isQuarterStart = (i % 8 == 0);
        const bool isSixteenthStart = (i % 2 == 0);

        if (enabled) {
            // Color-code pads: custom single/multi-FX pads glow with secondary accent, ALL FX pads with primary accent
            juce::Colour topCol = isCustomMask ? pal.accentSecondary : ((i >= 24) ? pal.accentSecondary.interpolatedWith(pal.stepActiveFill, 0.4f) : pal.stepActiveFill);
            if (isPlayhead) {
                topCol = topCol.brighter(0.45f);
            }
            juce::ColourGradient padGrad(topCol, r.getCentreX(), r.getY(),
                                         topCol.darker(0.55f), r.getCentreX(), r.getBottom(), false);
            g.setGradientFill(padGrad);
            g.fillRoundedRectangle(r, 3.5f);

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
            g.setColour(enabled ? pal.textBright.withAlpha(0.6f) : pal.cardBorder);
            g.drawRoundedRectangle(r, 3.5f, 1.0f);
        }

        // Step number label (1..32) at top of pad
        auto topLabelRect = r.withHeight(16.0f).withY(r.getY() + 2.0f);
        g.setColour(enabled ? juce::Colours::black.withAlpha(0.9f)
                            : (isSixteenthStart ? pal.textBright.withAlpha(0.75f) : pal.textMuted.withAlpha(0.55f)));
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText(juce::String(i + 1), topLabelRect.toNearestInt(), juce::Justification::centred);

        // Middle FX Badge ("ALL", "BC", "TS", "3FX", etc.)
        if (enabled) {
            auto badgeRect = r.withY(r.getY() + 19.0f).withHeight(16.0f).reduced(1.5f, 0.0f);
            g.setColour(juce::Colours::black.withAlpha(isCustomMask ? 0.68f : 0.38f));
            g.fillRoundedRectangle(badgeRect, 2.5f);

            g.setColour(isCustomMask ? pal.stepPlayheadOutline : pal.textBright);
            g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
            g.drawText(formatStepFxBadge(fxMask), badgeRect.toNearestInt(), juce::Justification::centred);
        }

        // Bottom 2x5 Mini LED Dot Matrix showing which of the 10 FX are active on this pad
        auto matrixArea = r.withTop(r.getBottom() - 16.0f).reduced(2.5f, 2.0f);
        const float cellW = matrixArea.getWidth() / 5.0f;
        const float cellH = matrixArea.getHeight() / 2.0f;
        for (int fx = 0; fx < 10; ++fx) {
            const int mRow = fx / 5;
            const int mCol = fx % 5;
            const float dx = matrixArea.getX() + static_cast<float>(mCol) * cellW + cellW * 0.5f;
            const float dy = matrixArea.getY() + static_cast<float>(mRow) * cellH + cellH * 0.5f;
            const bool fxBitOn = enabled && ((fxMask & (1u << fx)) != 0u);

            if (fxBitOn) {
                g.setColour(pal.textBright.withAlpha(0.95f));
                g.fillEllipse(dx - 1.5f, dy - 1.5f, 3.0f, 3.0f);
            } else {
                g.setColour(juce::Colours::black.withAlpha(0.45f));
                g.fillEllipse(dx - 1.1f, dy - 1.1f, 2.2f, 2.2f);
            }
        }
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
