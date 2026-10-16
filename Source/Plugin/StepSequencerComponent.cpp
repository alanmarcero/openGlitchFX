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
        const int val = static_cast<int>(std::round(raw->load()));
        return static_cast<uint16_t>(std::clamp(val, 0, static_cast<int>(kAllFx))) & kAllFx;
    }
    return kAllFx;
}

uint16_t StepSequencerComponent::getEffectiveStepFxMask(int stepIndex) const {
    if (!getStepParam(stepIndex)) return 0u;
    return getStepFxMaskParam(stepIndex);
}

StepSequencerComponent::ContiguousRun StepSequencerComponent::findContiguousRun(int stepIndex) const {
    stepIndex = std::clamp(stepIndex, 0, 31);
    const uint16_t mask = getEffectiveStepFxMask(stepIndex);
    if (mask == 0u) {
        return { stepIndex, stepIndex, 0u };
    }

    int start = stepIndex;
    while (start > 0 && getEffectiveStepFxMask(start - 1) == mask) {
        --start;
    }

    int end = stepIndex;
    while (end + 1 < 32 && getEffectiveStepFxMask(end + 1) == mask) {
        ++end;
    }

    return { start, end, mask };
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

    if (auto* onParam = apvts_.getParameter(desc.onParamId)) {
        if (onParam->getValue() < 0.5f) {
            onParam->beginChangeGesture();
            onParam->setValueNotifyingHost(1.0f);
            onParam->endChangeGesture();
        }
    }

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
        if (getStepBounds(i).expanded(2.0f, 2.0f).contains(pos)) {
            return i;
        }
    }
    return -1;
}

void StepSequencerComponent::applyPaintBrushToStep(int step, bool isInitialClick) {
    if (step < 0 || step >= 32) return;

    if (paintTargetFx_ < 0) {
        if (isInitialClick) {
            dragTargetState_ = !getStepParam(step);
        }
        setStepParam(step, dragTargetState_);
        if (dragTargetState_) {
            // If adjacent neighbor is enabled, inherit its FX mask so adjacent pads combine seamlessly!
            if (step > 0 && getStepParam(step - 1) && getStepFxMaskParam(step - 1) != 0u) {
                setStepFxMaskParam(step, getStepFxMaskParam(step - 1));
            } else if (step + 1 < 32 && getStepParam(step + 1) && getStepFxMaskParam(step + 1) != 0u) {
                setStepFxMaskParam(step, getStepFxMaskParam(step + 1));
            } else if (getStepFxMaskParam(step) == 0u) {
                setStepFxMaskParam(step, kAllFx);
            }
        }
    } else {
        const uint16_t fxBit = static_cast<uint16_t>(1u << paintTargetFx_);
        const bool stepWasOn = getStepParam(step);
        const uint16_t oldMask = getStepFxMaskParam(step);

        if (isInitialClick) {
            if (!stepWasOn || oldMask == kAllFx || (oldMask & fxBit) == 0u) {
                dragTargetState_ = true;
            } else {
                dragTargetState_ = false;
            }
        }

        if (dragTargetState_) {
            ensureFxAudible(paintTargetFx_);
            if (!stepWasOn || oldMask == kAllFx) {
                setStepFxMaskParam(step, fxBit);
            } else {
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
    const auto run = findContiguousRun(step);
    const bool isMergedGroup = stepOn && (run.length() > 1);

    juce::PopupMenu menu;
    if (isMergedGroup) {
        menu.addSectionHeader(juce::String::formatted("COMBINED BLOCK: PADS %02d-%02d (%d/32)  |  PAD %02d",
                                                      run.startStep + 1, run.endStep + 1, run.length(), step + 1));
    } else {
        menu.addSectionHeader(juce::String::formatted("PAD %02d/32 — EFFECT ROUTING", step + 1));
    }

    const juce::String targetLabel = isMergedGroup
        ? juce::String::formatted("Combined Block (Pads %02d-%02d)", run.startStep + 1, run.endStep + 1)
        : juce::String::formatted("Pad %02d", step + 1);

    menu.addItem(1, "Enable ALL 10 FX on " + targetLabel, true, stepOn && mask == kAllFx);
    menu.addItem(2, "Disable " + targetLabel + " (Dry Pass-Through)", true, !stepOn);

    menu.addSeparator();
    menu.addSectionHeader("SOLO SINGLE EFFECT ON " + targetLabel.toUpperCase());
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
    menu.addSectionHeader("TOGGLE MULTI-FX ON " + targetLabel.toUpperCase());
    for (int fx = 0; fx < 10; ++fx) {
        const auto& desc = openglitch::params::kFxDescriptors[static_cast<size_t>(fx)];
        const uint16_t bit = static_cast<uint16_t>(1u << fx);
        const bool fxChecked = stepOn && ((mask & bit) != 0u);
        menu.addItem(200 + fx,
                     juce::String::formatted("%d. %s [%s]", fx + 1, desc.name, desc.shortCode),
                     true,
                     fxChecked);
    }

    if (isMergedGroup) {
        juce::PopupMenu splitSubMenu;
        splitSubMenu.addItem(400, juce::String::formatted("Disable Only Pad %02d (Split Block)", step + 1));
        for (int fx = 0; fx < 10; ++fx) {
            const auto& desc = openglitch::params::kFxDescriptors[static_cast<size_t>(fx)];
            splitSubMenu.addItem(410 + fx,
                                 juce::String::formatted("Solo %s (%s) on Only Pad %02d", desc.name, desc.shortCode, step + 1));
        }
        menu.addSeparator();
        menu.addSubMenu(juce::String::formatted("Split / Edit Single Pad %02d Only", step + 1), splitSubMenu);
    }

    menu.addSeparator();
    menu.addItem(300, "Copy FX Routing to Current Beat (8 Pads)");
    menu.addItem(301, "Copy FX Routing to All 32 Pads");

    const int applyStart = isMergedGroup ? run.startStep : step;
    const int applyEnd   = isMergedGroup ? run.endStep   : step;

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                       [this, step, stepOn, mask, applyStart, applyEnd](int result) {
        if (result == 0) return;

        auto applyToRange = [&](uint16_t newMask, bool newState) {
            for (int s = applyStart; s <= applyEnd; ++s) {
                setStepFxMaskParam(s, newMask);
                setStepParam(s, newState);
            }
        };

        if (result == 1) {
            applyToRange(kAllFx, true);
        } else if (result == 2) {
            for (int s = applyStart; s <= applyEnd; ++s) {
                setStepParam(s, false);
            }
        } else if (result >= 100 && result < 110) {
            const int fx = result - 100;
            ensureFxAudible(fx);
            applyToRange(static_cast<uint16_t>(1u << fx), true);
        } else if (result >= 200 && result < 210) {
            const int fx = result - 200;
            const uint16_t bit = static_cast<uint16_t>(1u << fx);
            uint16_t baseMask = stepOn ? mask : 0u;
            if (baseMask == kAllFx) {
                baseMask = static_cast<uint16_t>(kAllFx & ~bit);
                applyToRange(baseMask, true);
            } else {
                const bool turningOn = (baseMask & bit) == 0u;
                if (turningOn) {
                    ensureFxAudible(fx);
                    baseMask |= bit;
                    applyToRange(baseMask, true);
                } else {
                    baseMask &= static_cast<uint16_t>(~bit);
                    if (baseMask == 0u) {
                        applyToRange(kAllFx, false);
                    } else {
                        applyToRange(baseMask, true);
                    }
                }
            }
        } else if (result == 400) {
            setStepParam(step, false);
        } else if (result >= 410 && result < 420) {
            const int fx = result - 410;
            ensureFxAudible(fx);
            setStepFxMaskParam(step, static_cast<uint16_t>(1u << fx));
            setStepParam(step, true);
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

juce::String StepSequencerComponent::formatStepFxBadge(uint16_t mask, int spanLen) {
    mask &= kAllFx;
    if (mask == 0u) return "OFF";

    juce::String fxText = "ALL";
    if (mask != kAllFx) {
        const int count = std::popcount(mask);
        if (count == 1) {
            for (int fx = 0; fx < 10; ++fx) {
                if ((mask & (1u << fx)) != 0u) {
                    const auto& desc = openglitch::params::kFxDescriptors[static_cast<size_t>(fx)];
                    fxText = (spanLen >= 4) ? juce::String(desc.name) : juce::String(desc.shortCode);
                    break;
                }
            }
        } else if (count == 2 && spanLen >= 2) {
            juce::StringArray codes;
            for (int fx = 0; fx < 10; ++fx) {
                if ((mask & (1u << fx)) != 0u) {
                    codes.add(openglitch::params::kFxDescriptors[static_cast<size_t>(fx)].shortCode);
                }
            }
            fxText = codes.joinIntoString("+");
        } else {
            fxText = juce::String(count) + "FX";
        }
    } else if (spanLen >= 6) {
        fxText = "ALL FX";
    }

    if (spanLen <= 1) {
        return fxText;
    }
    if (spanLen == 2) {
        return fxText + " 2/32";
    }
    if (spanLen == 4) {
        return fxText + " (4/32 • 1/8 BAR)";
    }
    if (spanLen == 8) {
        return fxText + " (8/32 • 1 BEAT)";
    }
    if (spanLen == 16) {
        return fxText + " (16/32 • 2 BEATS)";
    }
    if (spanLen == 32) {
        return fxText + " (32/32 • FULL BAR)";
    }
    return fxText + juce::String::formatted(" (%d/32)", spanLen);
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
        "BEAT 1 (1-8)  [ADJACENT PADS MERGE • R-CLICK FOR FX]",
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

    // PASS 1: Draw all DISABLED (inactive) 1/32 steps first
    for (int i = 0; i < 32; ++i) {
        if (getEffectiveStepFxMask(i) != 0u) {
            continue;
        }
        const auto r = getStepBounds(i);
        const bool isPlayhead = (i == currentStep_);
        const bool isQuarterStart = (i % 8 == 0);
        const bool isSixteenthStart = (i % 2 == 0);

        juce::Colour offCol = isQuarterStart ? pal.stepInactiveFill.brighter(0.15f)
                                             : pal.stepInactiveFill;
        g.setColour(offCol);
        g.fillRoundedRectangle(r, 3.5f);

        g.setColour(isPlayhead ? pal.stepPlayheadOutline : pal.cardBorder);
        g.drawRoundedRectangle(r, 3.5f, isPlayhead ? 2.0f : 1.0f);

        auto topLabelRect = r.withHeight(16.0f).withY(r.getY() + 2.0f);
        g.setColour(isSixteenthStart ? pal.textBright.withAlpha(0.7f) : pal.textMuted.withAlpha(0.5f));
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText(juce::String(i + 1), topLabelRect.toNearestInt(), juce::Justification::centred);
    }

    // PASS 2: Scan and visually COMBINE adjacent enabled pads with matching FX masks into unified capsule blocks!
    int stepIdx = 0;
    while (stepIdx < 32) {
        const uint16_t fxMask = getEffectiveStepFxMask(stepIdx);
        if (fxMask == 0u) {
            ++stepIdx;
            continue;
        }

        const auto run = findContiguousRun(stepIdx);
        const int runStart = run.startStep;
        const int runEnd   = run.endStep;
        const int runLen   = run.length();

        const auto firstRect = getStepBounds(runStart);
        const auto lastRect  = getStepBounds(runEnd);
        const juce::Rectangle<float> mergedRect(firstRect.getX(), firstRect.getY(),
                                                lastRect.getRight() - firstRect.getX(),
                                                firstRect.getHeight());

        const bool isCustomMask = (fxMask != kAllFx);
        const bool playheadInSpan = (currentStep_ >= runStart && currentStep_ <= runEnd);

        juce::Colour baseCol = isCustomMask
            ? pal.accentSecondary
            : ((runEnd >= 24) ? pal.accentSecondary.interpolatedWith(pal.stepActiveFill, 0.45f) : pal.stepActiveFill);

        if (playheadInSpan) {
            baseCol = baseCol.brighter(0.28f);
        }

        // Unified merged capsule gradient
        juce::ColourGradient mergedGrad(baseCol, mergedRect.getCentreX(), mergedRect.getY(),
                                        baseCol.darker(0.55f), mergedRect.getCentreX(), mergedRect.getBottom(), false);
        g.setGradientFill(mergedGrad);
        g.fillRoundedRectangle(mergedRect, 5.0f);

        // If playhead is inside this combined span, draw a continuous sweep highlight from runStart up to current playhead!
        if (playheadInSpan) {
            const float spanStartPhase = static_cast<float>(runStart) / 32.0f;
            const float spanTotalPhase = static_cast<float>(runLen) / 32.0f;
            const float localProgress = std::clamp((barPhase_ - spanStartPhase) / spanTotalPhase, 0.0f, 1.0f);

            juce::Rectangle<float> sweepRect = mergedRect.withWidth(mergedRect.getWidth() * localProgress);
            g.setColour(pal.textBright.withAlpha(0.22f));
            g.fillRoundedRectangle(sweepRect, 5.0f);

            // Outer glow around the entire combined block
            g.setColour(baseCol.withAlpha(0.45f));
            g.drawRoundedRectangle(mergedRect.expanded(2.0f), 6.0f, 2.5f);
        }

        // Subtle internal 1/32 subdivision ticks & step numbers along the top edge of the merged block
        for (int s = runStart; s <= runEnd; ++s) {
            const auto sr = getStepBounds(s);
            if (s > runStart) {
                const float tickX = (getStepBounds(s - 1).getRight() + sr.getX()) * 0.5f;
                g.setColour(juce::Colours::black.withAlpha(0.28f));
                g.drawLine(tickX, mergedRect.getY() + 2.0f, tickX, mergedRect.getY() + 15.0f, 1.0f);
            }

            auto topNumRect = sr.withHeight(14.0f).withY(sr.getY() + 2.0f);
            const bool isCurrentSubStep = (s == currentStep_);
            g.setColour(isCurrentSubStep ? pal.stepPlayheadOutline : juce::Colours::black.withAlpha(0.85f));
            g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
            g.drawText(juce::String(s + 1), topNumRect.toNearestInt(), juce::Justification::centred);
        }

        // Unified Border around the combined block
        g.setColour(playheadInSpan ? pal.stepPlayheadOutline : pal.textBright.withAlpha(0.75f));
        g.drawRoundedRectangle(mergedRect, 5.0f, playheadInSpan ? 2.2f : 1.3f);

        // Unified Center FX & Duration Badge across the combined block
        auto badgeRect = mergedRect.withY(mergedRect.getY() + 18.0f).withHeight(18.0f).reduced(3.0f, 0.0f);
        g.setColour(juce::Colours::black.withAlpha(0.62f));
        g.fillRoundedRectangle(badgeRect, 3.5f);

        g.setColour(isCustomMask ? pal.stepPlayheadOutline : pal.textBright);
        g.setFont(juce::FontOptions(runLen >= 2 ? 9.5f : 8.5f, juce::Font::bold));
        g.drawText(formatStepFxBadge(fxMask, runLen), badgeRect.toNearestInt(), juce::Justification::centred);

        // Unified 2x5 Mini LED Dot Matrix centered at the bottom of the combined block
        const float maxMatrixW = std::min(mergedRect.getWidth() - 6.0f, 80.0f);
        juce::Rectangle<float> matrixArea(mergedRect.getCentreX() - maxMatrixW * 0.5f,
                                          mergedRect.getBottom() - 15.0f,
                                          maxMatrixW,
                                          12.0f);
        const float cellW = matrixArea.getWidth() / 5.0f;
        const float cellH = matrixArea.getHeight() / 2.0f;
        for (int fx = 0; fx < 10; ++fx) {
            const int mRow = fx / 5;
            const int mCol = fx % 5;
            const float dx = matrixArea.getX() + static_cast<float>(mCol) * cellW + cellW * 0.5f;
            const float dy = matrixArea.getY() + static_cast<float>(mRow) * cellH + cellH * 0.5f;
            const bool fxBitOn = ((fxMask & (1u << fx)) != 0u);

            if (fxBitOn) {
                g.setColour(pal.textBright.withAlpha(0.95f));
                g.fillEllipse(dx - 1.7f, dy - 1.7f, 3.4f, 3.4f);
            } else {
                g.setColour(juce::Colours::black.withAlpha(0.45f));
                g.fillEllipse(dx - 1.2f, dy - 1.2f, 2.4f, 2.4f);
            }
        }

        stepIdx = runEnd + 1;
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
