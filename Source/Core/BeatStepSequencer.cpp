#include "BeatStepSequencer.h"
#include <algorithm>
#include <cmath>

namespace openglitch::core {

BeatStepSequencer::BeatStepSequencer() {
    steps_.fill(true);
    stepFxMasks_.fill(kAllFxMask);
    fxEnvelopes_.fill(1.0f);
    updateSmoothingCoeff();
}

void BeatStepSequencer::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    updateSmoothingCoeff();
    reset();
}

void BeatStepSequencer::reset() {
    internalPpq_ = 0.0;
    currentStep_ = 0;
    currentBarPhase_ = 0.0;
    const uint16_t activeMask0 = getEffectiveStepFxMask(0);
    wasActiveStep_ = false;
    wasActiveFxMask_ = 0u;
    envelope_ = activeMask0 != 0u ? 1.0f : 0.0f;
    for (int fx = 0; fx < kNumFxSlots; ++fx) {
        const bool fxOn = ((activeMask0 >> fx) & 1u) != 0u;
        fxEnvelopes_[static_cast<size_t>(fx)] = fxOn ? 1.0f : 0.0f;
    }
}

uint16_t BeatStepSequencer::getEffectiveStepFxMask(int stepIndex) const {
    if (stepIndex >= 0 && stepIndex < kNumSteps && steps_[static_cast<size_t>(stepIndex)]) {
        return stepFxMasks_[static_cast<size_t>(stepIndex)] & kAllFxMask;
    }
    return 0u;
}

void BeatStepSequencer::setStep(int stepIndex, bool enabled) {
    if (stepIndex >= 0 && stepIndex < kNumSteps) {
        steps_[static_cast<size_t>(stepIndex)] = enabled;
    }
}

bool BeatStepSequencer::getStep(int stepIndex) const {
    if (stepIndex >= 0 && stepIndex < kNumSteps) {
        return steps_[static_cast<size_t>(stepIndex)];
    }
    return false;
}

void BeatStepSequencer::setStepFxMask(int stepIndex, uint16_t fxMask) {
    if (stepIndex >= 0 && stepIndex < kNumSteps) {
        stepFxMasks_[static_cast<size_t>(stepIndex)] = (fxMask & kAllFxMask);
    }
}

uint16_t BeatStepSequencer::getStepFxMask(int stepIndex) const {
    if (stepIndex >= 0 && stepIndex < kNumSteps) {
        return stepFxMasks_[static_cast<size_t>(stepIndex)] & kAllFxMask;
    }
    return 0u;
}

void BeatStepSequencer::setStepFxEnabled(int stepIndex, int fxIndex, bool enabled) {
    if (stepIndex >= 0 && stepIndex < kNumSteps && fxIndex >= 0 && fxIndex < kNumFxSlots) {
        const uint16_t bit = static_cast<uint16_t>(1u << fxIndex);
        if (enabled) {
            stepFxMasks_[static_cast<size_t>(stepIndex)] |= bit;
        } else {
            stepFxMasks_[static_cast<size_t>(stepIndex)] &= static_cast<uint16_t>(~bit);
        }
        stepFxMasks_[static_cast<size_t>(stepIndex)] &= kAllFxMask;
    }
}

bool BeatStepSequencer::isStepFxEnabled(int stepIndex, int fxIndex) const {
    if (stepIndex >= 0 && stepIndex < kNumSteps && fxIndex >= 0 && fxIndex < kNumFxSlots) {
        return ((stepFxMasks_[static_cast<size_t>(stepIndex)] >> fxIndex) & 1u) != 0u;
    }
    return false;
}

void BeatStepSequencer::setAllStepsFxMask(uint16_t fxMask) {
    stepFxMasks_.fill(fxMask & kAllFxMask);
}

void BeatStepSequencer::setAllSteps(bool enabled) {
    steps_.fill(enabled);
}

void BeatStepSequencer::setPatternBitmask(uint32_t mask) {
    for (int i = 0; i < kNumSteps; ++i) {
        steps_[static_cast<size_t>(i)] = ((mask >> i) & 1u) != 0u;
    }
}

uint32_t BeatStepSequencer::getPatternBitmask() const {
    uint32_t mask = 0u;
    for (int i = 0; i < kNumSteps; ++i) {
        if (steps_[static_cast<size_t>(i)]) {
            mask |= (1u << i);
        }
    }
    return mask;
}

void BeatStepSequencer::applyPreset(StepPatternPreset preset) {
    steps_.fill(false);
    switch (preset) {
        case StepPatternPreset::AllOn:
            steps_.fill(true);
            break;
        case StepPatternPreset::LastBeat:
            for (int i = 24; i < 32; ++i) steps_[static_cast<size_t>(i)] = true;
            break;
        case StepPatternPreset::LastHalfBar:
            for (int i = 16; i < 32; ++i) steps_[static_cast<size_t>(i)] = true;
            break;
        case StepPatternPreset::LastFourSteps:
            for (int i = 28; i < 32; ++i) steps_[static_cast<size_t>(i)] = true;
            break;
        case StepPatternPreset::Last32nd:
            steps_[31] = true;
            break;
        case StepPatternPreset::Backbeat2And4:
            for (int i = 8; i < 16; ++i) steps_[static_cast<size_t>(i)] = true;
            for (int i = 24; i < 32; ++i) steps_[static_cast<size_t>(i)] = true;
            break;
        case StepPatternPreset::Stutter16ths:
            for (int i = 0; i < 32; ++i) {
                steps_[static_cast<size_t>(i)] = ((i / 2) % 2 == 1);
            }
            break;
        case StepPatternPreset::Every4thStep:
            for (int i = 3; i < 32; i += 4) {
                steps_[static_cast<size_t>(i)] = true;
            }
            break;
        case StepPatternPreset::ClearAll:
        default:
            break;
    }
}

void BeatStepSequencer::setExternalTriggerMode(bool enabled, bool quantizeToStep) {
    if (externalMode_ != enabled) {
        externalPendingMask_ = 0u;
        externalActiveMask_ = 0u;
    }
    externalMode_ = enabled;
    externalQuantize_ = quantizeToStep;
}

void BeatStepSequencer::setExternalFxMask(uint16_t fxMask) {
    externalPendingMask_ = fxMask & kAllFxMask;
}

// A MIDI note has no pad span, so time-based FX use one beat.
StepSpanInfo BeatStepSequencer::getExternalSpan() const {
    constexpr int kNoteSpanSteps = kNumSteps / 4;
    const auto phase = static_cast<float>(std::clamp(externalElapsedPpq_, 0.0, 1.0));
    return { currentStep_, currentStep_, kNoteSpanSteps, phase };
}

StepSpanInfo BeatStepSequencer::getMergedPadSpan(int stepIndex) const {
    if (externalMode_) {
        return getExternalSpan();
    }
    stepIndex = std::clamp(stepIndex, 0, kNumSteps - 1);
    const uint16_t mask = getEffectiveStepFxMask(stepIndex);
    if (mask == 0u) {
        return { stepIndex, stepIndex, 1, 0.0f };
    }

    return getCircularSpan(stepIndex, mask, true);
}

StepSpanInfo BeatStepSequencer::getActiveFxSpan(int stepIndex, int fxIndex) const {
    if (externalMode_) {
        return getExternalSpan();
    }
    stepIndex = std::clamp(stepIndex, 0, kNumSteps - 1);
    if (fxIndex < 0 || fxIndex >= kNumFxSlots) {
        return { stepIndex, stepIndex, 1, 0.0f };
    }

    const uint16_t bit = static_cast<uint16_t>(1u << fxIndex);
    if ((getEffectiveStepFxMask(stepIndex) & bit) == 0u) {
        return { stepIndex, stepIndex, 1, 0.0f };
    }

    return getCircularSpan(stepIndex, bit, false);
}

StepSpanInfo BeatStepSequencer::getCircularSpan(int stepIndex, uint16_t mask, bool exactMask) const {
    const auto matches = [&](int step) {
        const uint16_t candidate = getEffectiveStepFxMask(step);
        return exactMask ? candidate == mask : (candidate & mask) != 0u;
    };
    int start = stepIndex;
    int len = 1;
    while (len < kNumSteps) {
        const int previous = (start + kNumSteps - 1) % kNumSteps;
        if (!matches(previous)) break;
        start = previous;
        ++len;
    }
    if (len == kNumSteps) {
        start = 0;
    } else {
        int next = (stepIndex + 1) % kNumSteps;
        while (len < kNumSteps && matches(next)) {
            ++len;
            next = (next + 1) % kNumSteps;
        }
    }
    const int end = (start + len - 1) % kNumSteps;
    const double offset = std::fmod(currentBarPhase_ * kNumSteps - start + kNumSteps, kNumSteps);
    const float spanPhase = offset < len ? static_cast<float>(offset / len) : 0.0f;

    return { start, end, len, spanPhase };
}

void BeatStepSequencer::setSmoothingTimeMs(float ms) {
    smoothingTimeMs_ = std::clamp(ms, 0.0f, 50.0f);
    updateSmoothingCoeff();
}

void BeatStepSequencer::updateSmoothingCoeff() {
    const double tauSeconds = static_cast<double>(smoothingTimeMs_) * 0.001;
    if (tauSeconds <= 0.0001 || sampleRate_ <= 0.0) {
        smoothCoeff_ = 0.0f;
    } else {
        smoothCoeff_ = static_cast<float>(std::exp(-1.0 / (tauSeconds * sampleRate_)));
    }
}

int BeatStepSequencer::computeStepFromPpq(double ppqPosition, double* outBarPhase) {
    double barPpq = std::fmod(ppqPosition, kQuarterNotesPerBar);
    if (barPpq < 0.0) {
        barPpq += kQuarterNotesPerBar;
    }
    const double phase = std::clamp(barPpq / kQuarterNotesPerBar, 0.0, 0.999999999);
    if (outBarPhase != nullptr) {
        *outBarPhase = phase;
    }
    const int step = static_cast<int>(phase * static_cast<double>(kNumSteps));
    return std::clamp(step, 0, kNumSteps - 1);
}

bool BeatStepSequencer::isCurrentStepEnabled() const {
    return getCurrentStepActiveFxMask() != 0u;
}

uint16_t BeatStepSequencer::getCurrentStepActiveFxMask() const {
    if (externalMode_) {
        return externalActiveMask_;
    }
    return getEffectiveStepFxMask(currentStep_);
}

float BeatStepSequencer::getFxEnvelope(int fxIndex) const {
    if (fxIndex >= 0 && fxIndex < kNumFxSlots) {
        return fxEnvelopes_[static_cast<size_t>(fxIndex)];
    }
    return 0.0f;
}

float BeatStepSequencer::advanceSample(double bpm,
                                       bool useHostSync,
                                       bool hasValidHostPpq,
                                       double hostPpqAtSample,
                                       bool& stepTransitionedToActive) {
    uint16_t dummyFxTransitionMask = 0u;
    return advanceSampleWithFxMask(bpm, useHostSync, hasValidHostPpq, hostPpqAtSample,
                                   stepTransitionedToActive, dummyFxTransitionMask);
}

float BeatStepSequencer::advanceSampleWithFxMask(double bpm,
                                                 bool useHostSync,
                                                 bool hasValidHostPpq,
                                                 double hostPpqAtSample,
                                                 bool& stepTransitionedToActive,
                                                 uint16_t& fxTransitionedToActiveMask) {
    const double safeBpm = std::clamp(bpm, 20.0, 300.0);
    double effectivePpq = 0.0;

    if (useHostSync && hasValidHostPpq) {
        effectivePpq = hostPpqAtSample;
        internalPpq_ = hostPpqAtSample;
    } else {
        effectivePpq = internalPpq_;
        const double ppqPerSample = (safeBpm / 60.0) / sampleRate_;
        internalPpq_ += ppqPerSample;
        if (internalPpq_ >= 1024.0) {
            internalPpq_ = std::fmod(internalPpq_, kQuarterNotesPerBar);
        }
    }

    const int previousStep = currentStep_;
    currentStep_ = computeStepFromPpq(effectivePpq, &currentBarPhase_);
    if (externalMode_ && (!externalQuantize_ || currentStep_ != previousStep)) {
        if (externalActiveMask_ == 0u && externalPendingMask_ != 0u) {
            externalElapsedPpq_ = 0.0;
        }
        externalActiveMask_ = externalPendingMask_;
    }
    externalElapsedPpq_ += (safeBpm / 60.0) / sampleRate_;
    const uint16_t activeFxMaskNow = getCurrentStepActiveFxMask();
    const bool activeNow = (activeFxMaskNow != 0u);

    // CRITICAL: When moving between two adjacent enabled pads that share an effect,
    // `wasActiveFxMask_` already has that effect's bit set, so `activeFxMaskNow & ~wasActiveFxMask_`
    // is 0 for that effect -- meaning the effect CONTINUES seamlessly without resetting!
    stepTransitionedToActive = (!wasActiveStep_ && activeNow);
    fxTransitionedToActiveMask = static_cast<uint16_t>(activeFxMaskNow & ~wasActiveFxMask_);

    wasActiveStep_ = activeNow;
    wasActiveFxMask_ = activeFxMaskNow;

    const float target = activeNow ? 1.0f : 0.0f;
    envelope_ = target + smoothCoeff_ * (envelope_ - target);
    if (envelope_ < 1e-5f) envelope_ = 0.0f;
    if (envelope_ > 0.99999f) envelope_ = 1.0f;

    for (int fx = 0; fx < kNumFxSlots; ++fx) {
        const bool fxActive = ((activeFxMaskNow >> fx) & 1u) != 0u;
        const float fxTarget = fxActive ? 1.0f : 0.0f;
        float& fxEnv = fxEnvelopes_[static_cast<size_t>(fx)];
        fxEnv = fxTarget + smoothCoeff_ * (fxEnv - fxTarget);
        if (fxEnv < 1e-5f) fxEnv = 0.0f;
        if (fxEnv > 0.99999f) fxEnv = 1.0f;
    }

    return envelope_;
}

} // namespace openglitch::core
