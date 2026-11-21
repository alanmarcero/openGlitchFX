#include "StepSequencerComponent.h"
#include <algorithm>
#include <bit>
#include <numeric>
#include <random>
#include <ranges>

namespace openglitch::ui {

namespace {
constexpr int kNumFx = openglitch::core::BeatStepSequencer::kNumFxSlots;
constexpr uint16_t kAllFx = openglitch::core::BeatStepSequencer::kAllFxMask;
} // namespace

StepSequencerComponent::StepSequencerComponent(juce::AudioProcessorValueTreeState& apvts,
                                               const GlitchLookAndFeel& lnf,
                                               openglitch::suite::ParameterUndo& undo)
    : apvts_(apvts), lnf_(lnf), undo_(undo) {
}

StepSequencerComponent::~StepSequencerComponent() {
    if (gestureOpen_) {
        gestureOpen_ = false;
        undo_.endGesture();
    }
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

void StepSequencerComponent::setMidiTriggerMode(bool midiMode) {
    if (midiMode_ != midiMode) {
        midiMode_ = midiMode;
        repaint();
    }
}

void StepSequencerComponent::refreshPattern() {
    std::array<int, 34> shape{};
    std::ranges::generate_n(shape.begin(), 32, [this, step = 0]() mutable { return static_cast<int>(getEffectiveStepFxMask(step++)); });
    shape[32] = loopLength();
    shape[33] = stepsPerBeat();
    if (shape != shownShape_) {
        shownShape_ = shape;
        repaint();
    }
}

int StepSequencerComponent::loopLength() const {
    return juce::roundToInt(apvts_.getRawParameterValue(openglitch::params::kSequenceLength)->load());
}

// RATE 1/2x, 1x and 2x make a beat 4, 8 or 16 pads.
int StepSequencerComponent::stepsPerBeat() const {
    return 4 << juce::roundToInt(apvts_.getRawParameterValue(openglitch::params::kPatternRate)->load());
}

// The musical length of a pad span, such as "1/16", "3/32", "1 BEAT" or "2 BARS".
juce::String StepSequencerComponent::spanDuration(int steps, int stepsPerBeat) {
    const int stepsPerBar = stepsPerBeat * 4;
    if (steps % stepsPerBar == 0) {
        return juce::String(steps / stepsPerBar) + (steps == stepsPerBar ? " BAR" : " BARS");
    }
    if (steps % stepsPerBeat == 0) {
        return juce::String(steps / stepsPerBeat) + (steps == stepsPerBeat ? " BEAT" : " BEATS");
    }
    const int common = std::gcd(steps, stepsPerBar);
    return juce::String(steps / common) + "/" + juce::String(stepsPerBar / common);
}

juce::String StepSequencerComponent::getTooltip() {
    if (midiMode_) {
        return "TRIGGER is set to MIDI, so the pads are ignored. Notes 60-74 trigger one FX each; any other note triggers every FX.";
    }
    return "Click a pad to turn it on or off, drag to paint. Right-click for its FX routing. "
           "Adjacent pads with the same FX join into one block, and time-based FX run once across the block.";
}

void StepSequencerComponent::setPaintTargetFx(int fxIndex) {
    paintTargetFx_ = std::clamp(fxIndex, -1, kNumFx - 1);
    repaint();
}

int StepSequencerComponent::countStepsRoutingToFx(int fxIndex) const {
    if (fxIndex < 0 || fxIndex >= kNumFx) {
        return 0;
    }
    const uint16_t bit = static_cast<uint16_t>(1u << fxIndex);
    const int length = loopLength();
    int count = 0;
    for (int i = 0; i < length; ++i) {
        if ((getEffectiveStepFxMask(i) & bit) != 0u) {
            ++count;
        }
    }
    return count;
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
    return openglitch::params::readStepFxMask(apvts_, stepIndex);
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

    // Only pads with identical FX join, as in BeatStepSequencer::getMergedPadSpan.
    // A run stops at the loop end, so pads past LENGTH never join a played block.
    const int length = loopLength();
    const int first = stepIndex < length ? 0 : length;
    const int last = stepIndex < length ? length - 1 : 31;
    int start = stepIndex;
    while (start > first && getEffectiveStepFxMask(start - 1) == mask) {
        --start;
    }

    int end = stepIndex;
    while (end < last && getEffectiveStepFxMask(end + 1) == mask) {
        ++end;
    }

    return { start, end, mask };
}

void StepSequencerComponent::setStepFxMaskParam(int stepIndex, uint16_t mask) {
    if (stepIndex < 0 || stepIndex >= 32) return;
    const uint16_t clamped = mask & kAllFx;
    const std::array<juce::String, 2> ids = { openglitch::params::stepFxParamId(stepIndex),
                                            openglitch::params::stepExtraFxParamId(stepIndex) };
    const std::array<uint16_t, 2> values = { static_cast<uint16_t>(clamped & 1023u),
                                           static_cast<uint16_t>(clamped >> 10) };
    for (size_t i = 0; i < ids.size(); ++i) {
        if (auto* param = apvts_.getParameter(ids[i])) {
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(values[i])));
            param->endChangeGesture();
        }
    }
}

void StepSequencerComponent::ensureFxAudible(int fxIndex) {
    if (fxIndex < 0 || fxIndex >= kNumFx) return;
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
        if (stepOn && paintTargetFx_ >= 0 && paintTargetFx_ < kNumFx) {
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

void StepSequencerComponent::randomizePattern(uint32_t seed, int intensity) {
    const int level = std::clamp(intensity, 1, 3);
    const std::array<int, 3> activePercent = { 35, 55, 75 };
    const int activeThreshold = activePercent[static_cast<size_t>(level - 1)];
    std::mt19937 generator(seed);
    std::uniform_int_distribution<int> roll(0, 99);
    std::uniform_int_distribution<int> allFx(0, kNumFx - 1);

    if (paintTargetFx_ >= 0) {
        const uint16_t selectedBit = static_cast<uint16_t>(1u << paintTargetFx_);
        for (int step = 0; step < 32; ++step) {
            const uint16_t currentMask = getStepFxMaskParam(step);
            const bool routeSelectedFx = roll(generator) < activeThreshold;
            const uint16_t updatedMask = routeSelectedFx
                ? static_cast<uint16_t>(currentMask | selectedBit)
                : static_cast<uint16_t>(currentMask & ~selectedBit);
            setStepFxMaskParam(step, updatedMask);
        }
        ensureFxAudible(paintTargetFx_);
        repaint();
        return;
    }

    const uint16_t maskByIntensity = level == 1 ? 0x001fu : (level == 2 ? 0x03ffu : kAllFx);
    std::uniform_int_distribution<int> commonFx(0, level == 1 ? 4 : 9);
    uint16_t usedFxMask = 0u;
    int activeSteps = 0;
    for (int step = 0; step < 32; ++step) {
        const bool active = roll(generator) < activeThreshold;
        setStepParam(step, active);
        uint16_t fxMask = 0u;
        if (active) {
            fxMask = static_cast<uint16_t>(generator() & maskByIntensity);
            if (fxMask == 0u) {
                const int fallbackFx = level == 3 ? allFx(generator) : commonFx(generator);
                fxMask = static_cast<uint16_t>(1u << fallbackFx);
            }
            usedFxMask |= fxMask;
            ++activeSteps;
        }
        setStepFxMaskParam(step, fxMask);
    }

    if (activeSteps == 0) {
        const int fallbackFx = level == 3 ? allFx(generator) : commonFx(generator);
        const uint16_t fallbackMask = static_cast<uint16_t>(1u << fallbackFx);
        setStepParam(0, true);
        setStepFxMaskParam(0, fallbackMask);
        usedFxMask |= fallbackMask;
    }

    for (int fx = 0; fx < kNumFx; ++fx) {
        if ((usedFxMask & static_cast<uint16_t>(1u << fx)) != 0u) {
            ensureFxAudible(fx);
        }
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
            // Unify FX mask across the entire connected run of adjacent enabled pads so they link and sustain seamlessly!
            uint16_t targetMask = getStepFxMaskParam(step);
            if (step > 0 && getStepParam(step - 1) && getStepFxMaskParam(step - 1) != 0u) {
                targetMask = getStepFxMaskParam(step - 1);
            } else if (step + 1 < 32 && getStepParam(step + 1) && getStepFxMaskParam(step + 1) != 0u) {
                targetMask = getStepFxMaskParam(step + 1);
            }
            if (targetMask == 0u) {
                targetMask = kAllFx;
            }
            int s0 = step;
            while (s0 > 0 && getStepParam(s0 - 1)) --s0;
            int s1 = step;
            while (s1 + 1 < 32 && getStepParam(s1 + 1)) ++s1;
            for (int s = s0; s <= s1; ++s) {
                setStepFxMaskParam(s, targetMask);
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
    menu.setLookAndFeel(&getLookAndFeel());
    if (isMergedGroup) {
        menu.addSectionHeader(juce::String::formatted("COMBINED BLOCK: PADS %02d-%02d (", run.startStep + 1, run.endStep + 1)
                              + spanDuration(run.length(), stepsPerBeat()) + juce::String::formatted(")  |  PAD %02d", step + 1));
    } else {
        menu.addSectionHeader(juce::String::formatted("PAD %02d - EFFECT ROUTING", step + 1));
    }

    const juce::String targetLabel = isMergedGroup
        ? juce::String::formatted("Combined Block (Pads %02d-%02d)", run.startStep + 1, run.endStep + 1)
        : juce::String::formatted("Pad %02d", step + 1);

    menu.addItem(1, "Enable ALL 15 FX on " + targetLabel, true, stepOn && mask == kAllFx);
    menu.addItem(2, "Disable " + targetLabel + " (Dry Pass-Through)", true, !stepOn);

    menu.addSeparator();
    menu.addSectionHeader("SOLO SINGLE EFFECT ON " + targetLabel.toUpperCase());
    for (int fx = 0; fx < kNumFx; ++fx) {
        const auto& desc = openglitch::params::kFxDescriptors[static_cast<size_t>(fx)];
        const uint16_t singleBit = static_cast<uint16_t>(1u << fx);
        const bool isSoloed = stepOn && (mask == singleBit);
        menu.addItem(100 + fx,
                     "Only " + juce::String(desc.name) + " (" + desc.shortCode + ")",
                     true,
                     isSoloed);
    }

    menu.addSeparator();
    menu.addSectionHeader("TOGGLE MULTI-FX ON " + targetLabel.toUpperCase());
    for (int fx = 0; fx < kNumFx; ++fx) {
        const auto& desc = openglitch::params::kFxDescriptors[static_cast<size_t>(fx)];
        const uint16_t bit = static_cast<uint16_t>(1u << fx);
        const bool fxChecked = stepOn && ((mask & bit) != 0u);
        menu.addItem(200 + fx,
                     juce::String(fx + 1) + ". " + desc.name + " [" + desc.shortCode + "]",
                     true,
                     fxChecked);
    }

    if (isMergedGroup) {
        juce::PopupMenu splitSubMenu;
        splitSubMenu.setLookAndFeel(&getLookAndFeel());
        splitSubMenu.addItem(400, juce::String::formatted("Disable Only Pad %02d (Split Block)", step + 1));
        for (int fx = 0; fx < kNumFx; ++fx) {
            const auto& desc = openglitch::params::kFxDescriptors[static_cast<size_t>(fx)];
            splitSubMenu.addItem(410 + fx, "Solo " + juce::String(desc.name) + " (" + desc.shortCode + ") on Only Pad "
                                               + juce::String(step + 1).paddedLeft('0', 2));
        }
        menu.addSeparator();
        menu.addSubMenu(juce::String::formatted("Split / Edit Single Pad %02d Only", step + 1), splitSubMenu);
    }

    menu.addSeparator();
    const int groupStart = (step / 8) * 8;
    menu.addItem(300, juce::String::formatted("Copy This Pad (On/Off + FX) to Pads %02d-%02d", groupStart + 1, groupStart + 8));
    menu.addItem(301, "Copy FX Routing to All 32 Pads");

    const int applyStart = isMergedGroup ? run.startStep : step;
    const int applyEnd   = isMergedGroup ? run.endStep   : step;

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withDeletionCheck(*this),
                       [this, step, stepOn, mask, applyStart, applyEnd](int result) {
        if (result == 0) return;
        undo_.perform("Edit steps", [&] {
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
        } else if (result >= 100 && result < 100 + kNumFx) {
            const int fx = result - 100;
            ensureFxAudible(fx);
            applyToRange(static_cast<uint16_t>(1u << fx), true);
        } else if (result >= 200 && result < 200 + kNumFx) {
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
        } else if (result >= 410 && result < 410 + kNumFx) {
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
        });
        repaint();
    });
}

void StepSequencerComponent::mouseMove(const juce::MouseEvent& event) {
    const int step = getStepAtPosition(event.position);
    if (step != hoveredStep_) {
        hoveredStep_ = step;
        repaint();
    }
}

void StepSequencerComponent::mouseExit(const juce::MouseEvent& /*event*/) {
    if (hoveredStep_ != -1) {
        hoveredStep_ = -1;
        repaint();
    }
}

void StepSequencerComponent::mouseDown(const juce::MouseEvent& event) {
    const int step = getStepAtPosition(event.position);
    if (step < 0) return;

    if (event.mods.isPopupMenu()) {
        showStepContextMenu(step);
        return;
    }

    undo_.beginGesture("Edit steps");
    gestureOpen_ = true;
    applyPaintBrushToStep(step, true);
}

void StepSequencerComponent::mouseUp(const juce::MouseEvent&) {
    if (gestureOpen_) {
        gestureOpen_ = false;
        undo_.endGesture();
    }
}

void StepSequencerComponent::mouseDrag(const juce::MouseEvent& event) {
    if (event.mods.isPopupMenu()) return;

    const int step = getStepAtPosition(event.position);
    if (step != hoveredStep_) {
        hoveredStep_ = step;
    }
    if (step >= 0) {
        applyPaintBrushToStep(step, false);
    }
}

juce::String StepSequencerComponent::formatStepFxBadge(uint16_t mask, int spanLen, int stepsPerBeat) {
    mask &= kAllFx;
    if (mask == 0u) return "OFF";

    juce::String fxText = "ALL";
    if (mask != kAllFx) {
        const int count = std::popcount(mask);
        if (count == 1) {
            for (int fx = 0; fx < kNumFx; ++fx) {
                if ((mask & (1u << fx)) != 0u) {
                    const auto& desc = openglitch::params::kFxDescriptors[static_cast<size_t>(fx)];
                    fxText = (spanLen >= 4) ? juce::String(desc.name) : juce::String(desc.shortCode);
                    break;
                }
            }
        } else if (count == 2 && spanLen >= 2) {
            juce::StringArray codes;
            for (int fx = 0; fx < kNumFx; ++fx) {
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
    return fxText + " " + spanDuration(spanLen, stepsPerBeat);
}

void StepSequencerComponent::paint(juce::Graphics& g) {
    const auto& pal = lnf_.getPalette();
    const auto fullBounds = getLocalBounds().toFloat();

    // Background Card
    g.setColour(pal.cardBg);
    g.fillRoundedRectangle(fullBounds, 6.0f);
    g.setColour(pal.cardBorder);
    g.drawRoundedRectangle(fullBounds, 6.0f, 1.2f);

    // One header per beat, or one readout for the hovered pad
    const int length = loopLength();
    const int beatSteps = stepsPerBeat();

    if (hoveredStep_ >= 0 && hoveredStep_ < 32) {
        const uint16_t hMask = getEffectiveStepFxMask(hoveredStep_);
        const auto hRun = findContiguousRun(hoveredStep_);
        const auto readout = [&]() -> juce::String {
            if (hoveredStep_ >= length) {
                return juce::String::formatted("PAD %02d: AFTER LENGTH %d, NOT PLAYED  |  RIGHT-CLICK FOR FX ROUTING", hoveredStep_ + 1, length);
            }
            if (hMask == 0u) {
                return juce::String::formatted("PAD %02d/%d: OFF (DRY)  |  CLICK TO TURN ON  |  RIGHT-CLICK FOR FX ROUTING", hoveredStep_ + 1, length);
            }
            return juce::String::formatted("PAD %02d/%d  |  BLOCK %02d-%02d (", hoveredStep_ + 1, length, hRun.startStep + 1, hRun.endStep + 1)
                + spanDuration(hRun.length(), beatSteps) + ")  |  FX: " + formatStepFxBadge(hMask, 1, beatSteps)
                + "  |  RIGHT-CLICK FOR FX ROUTING";
        }();
        const auto firstStepRect = getStepBounds(0);
        const auto lastStepRect = getStepBounds(31);
        const juce::Rectangle<float> header(firstStepRect.getX(), fullBounds.getY() + 5.0f,
                                            lastStepRect.getRight() - firstStepRect.getX(), 18.0f);
        g.setColour(pal.accentSecondary.withAlpha(0.20f));
        g.fillRoundedRectangle(header, 3.0f);
        g.setColour(pal.textBright);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(readout, header.toNearestInt(), juce::Justification::centred);
    }

    for (int b = 0; b < 32 / beatSteps && hoveredStep_ < 0; ++b) {
        const int firstStep = b * beatSteps;
        const int lastStep = firstStep + beatSteps - 1;
        const auto firstStepRect = getStepBounds(firstStep);
        const auto lastStepRect  = getStepBounds(lastStep);
        juce::Rectangle<float> beatHeader(firstStepRect.getX(), fullBounds.getY() + 5.0f,
                                          lastStepRect.getRight() - firstStepRect.getX(), 18.0f);

        const bool playheadInBeat = (currentStep_ / beatSteps) == b;
        g.setColour(playheadInBeat ? pal.accentPrimary.withAlpha(0.18f) : pal.headerPlate.withAlpha(0.7f));
        g.fillRoundedRectangle(beatHeader, 3.0f);

        g.setColour(playheadInBeat ? pal.accentPrimary : pal.textMuted.withAlpha(firstStep < length ? 1.0f : 0.45f));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(juce::String::formatted("BEAT %d (%d-%d)", b + 1, firstStep + 1, lastStep + 1),
                   beatHeader.toNearestInt(), juce::Justification::centred);
    }

    // PASS 1: Draw all DISABLED (inactive) 1/32 steps first
    for (int i = 0; i < 32; ++i) {
        if (getEffectiveStepFxMask(i) != 0u) {
            continue;
        }
        const auto r = getStepBounds(i);
        const bool isPlayhead = (i == currentStep_);
        const bool isHovered = (i == hoveredStep_);
        const bool isQuarterStart = (i % 8 == 0);
        const bool isSixteenthStart = (i % 2 == 0);

        juce::Colour offCol = isQuarterStart ? pal.stepInactiveFill.brighter(0.15f)
                                             : pal.stepInactiveFill;
        if (isHovered) {
            offCol = offCol.brighter(0.25f);
        }
        g.setColour(offCol);
        g.fillRoundedRectangle(r, 3.5f);

        g.setColour(isPlayhead ? pal.stepPlayheadOutline : (isHovered ? pal.accentPrimary : pal.cardBorder));
        g.drawRoundedRectangle(r, 3.5f, (isPlayhead || isHovered) ? 1.8f : 1.0f);

        auto topLabelRect = r.withHeight(16.0f).withY(r.getY() + 2.0f);
        g.setColour(isSixteenthStart ? pal.textBright.withAlpha(0.75f) : pal.textMuted.withAlpha(0.65f));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
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
        const uint16_t runMask = run.fxMask;

        const auto firstRect = getStepBounds(runStart);
        const auto lastRect  = getStepBounds(runEnd);
        const juce::Rectangle<float> mergedRect(firstRect.getX(), firstRect.getY(),
                                                lastRect.getRight() - firstRect.getX(),
                                                firstRect.getHeight());

        const bool isCustomMask = (runMask != kAllFx);
        const bool playheadInSpan = (currentStep_ >= runStart && currentStep_ <= runEnd);
        const bool hoveredInSpan = (hoveredStep_ >= runStart && hoveredStep_ <= runEnd);

        juce::Colour baseCol = isCustomMask
            ? pal.accentSecondary
            : ((runEnd >= 24) ? pal.accentSecondary.interpolatedWith(pal.stepActiveFill, 0.45f) : pal.stepActiveFill);

        if (playheadInSpan || hoveredInSpan) {
            baseCol = baseCol.brighter(playheadInSpan ? 0.28f : 0.16f);
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

            auto topNumRect = sr.withHeight(16.0f).withY(sr.getY() + 2.0f);
            const bool isCurrentSubStep = (s == currentStep_);
            g.setColour(isCurrentSubStep ? pal.stepPlayheadOutline : juce::Colours::black.withAlpha(0.85f));
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText(juce::String(s + 1), topNumRect.toNearestInt(), juce::Justification::centred);
        }

        // Unified Border around the combined block
        g.setColour(playheadInSpan ? pal.stepPlayheadOutline : (hoveredInSpan ? pal.accentPrimary : pal.textBright.withAlpha(0.75f)));
        g.drawRoundedRectangle(mergedRect, 5.0f, playheadInSpan ? 2.2f : (hoveredInSpan ? 1.8f : 1.3f));

        // Unified Center FX & Duration Badge across the combined block
        auto badgeRect = mergedRect.withY(mergedRect.getY() + 18.0f).withHeight(18.0f).reduced(3.0f, 0.0f);
        g.setColour(juce::Colours::black.withAlpha(0.62f));
        g.fillRoundedRectangle(badgeRect, 3.5f);

        g.setColour(isCustomMask ? pal.stepPlayheadOutline : pal.textBright);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(formatStepFxBadge(runMask, runLen, beatSteps), badgeRect.toNearestInt(), juce::Justification::centred);

        // Unified 3x5 Mini LED Dot Matrix centered at the bottom of the combined block
        const float maxMatrixW = std::min(mergedRect.getWidth() - 6.0f, 80.0f);
        juce::Rectangle<float> matrixArea(mergedRect.getCentreX() - maxMatrixW * 0.5f,
                                          mergedRect.getBottom() - 21.0f,
                                          maxMatrixW,
                                          18.0f);
        const float cellW = matrixArea.getWidth() / 5.0f;
        const float cellH = matrixArea.getHeight() / 3.0f;
        for (int fx = 0; fx < kNumFx; ++fx) {
            const int mRow = fx / 5;
            const int mCol = fx % 5;
            const float dx = matrixArea.getX() + static_cast<float>(mCol) * cellW + cellW * 0.5f;
            const float dy = matrixArea.getY() + static_cast<float>(mRow) * cellH + cellH * 0.5f;
            const bool fxBitOn = ((runMask & (1u << fx)) != 0u);

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

    // Pads past LENGTH stay editable but never play: hatch them and mark the loop end.
    if (length < 32) {
        const auto lastPlayed = getStepBounds(length - 1);
        const auto skipped = getStepBounds(length).withRight(getStepBounds(31).getRight()).expanded(2.0f);
        g.setColour(pal.cardBg.withAlpha(0.6f));
        g.fillRect(skipped);
        juce::Path hatch;
        const int lineCount = juce::roundToInt((skipped.getWidth() + skipped.getHeight()) / 8.0f) + 1;
        std::ranges::for_each(std::views::iota(0, lineCount), [&](int line) {
            const float x = skipped.getX() - skipped.getHeight() + 8.0f * static_cast<float>(line);
            hatch.addLineSegment({ x, skipped.getBottom(), x + skipped.getHeight(), skipped.getY() }, 1.0f);
        });
        {
            juce::Graphics::ScopedSaveState clip(g);
            g.reduceClipRegion(skipped.toNearestInt());
            g.setColour(pal.textMuted.withAlpha(0.35f));
            g.fillPath(hatch);
        }
        const float loopEndX = (lastPlayed.getRight() + skipped.getX() + 2.0f) * 0.5f;
        g.setColour(pal.accentSecondary);
        g.fillRect(loopEndX - 1.0f, lastPlayed.getY() - 3.0f, 2.0f, lastPlayed.getHeight() + 12.0f);
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

    if (midiMode_) {
        const auto grid = firstRect.getUnion(lastRect);
        g.setColour(pal.cardBg.withAlpha(0.72f));
        g.fillRoundedRectangle(grid, 5.0f);
        g.setColour(pal.textBright);
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText("TRIGGER: MIDI  |  PADS IGNORED  |  NOTES 60-74 = ONE FX EACH, OTHER NOTES = ALL FX",
                   grid.toNearestInt(), juce::Justification::centred);
    }
}

} // namespace openglitch::ui
