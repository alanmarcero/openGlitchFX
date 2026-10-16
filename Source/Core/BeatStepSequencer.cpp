#include "BeatStepSequencer.h"
#include <algorithm>
#include <cmath>

namespace openglitch::core {

BeatStepSequencer::BeatStepSequencer() {
    // Default to all steps ON so signal passes through glitch FX out-of-the-box,
    // or user can toggle any of the 32 steps / presets.
    steps_.fill(true);
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
    wasActiveStep_ = steps_[0];
    envelope_ = wasActiveStep_ ? 1.0f : 0.0f;
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

void BeatStepSequencer::setSmoothingTimeMs(float ms) {
    smoothingTimeMs_ = std::clamp(ms, 0.1f, 50.0f);
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
    return getStep(currentStep_);
}

float BeatStepSequencer::advanceSample(double bpm,
                                       bool useHostSync,
                                       bool hasValidHostPpq,
                                       double hostPpqAtSample,
                                       bool& stepTransitionedToActive) {
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

    currentStep_ = computeStepFromPpq(effectivePpq, &currentBarPhase_);
    const bool activeNow = steps_[static_cast<size_t>(currentStep_)];

    stepTransitionedToActive = (!wasActiveStep_ && activeNow);
    wasActiveStep_ = activeNow;

    const float target = activeNow ? 1.0f : 0.0f;
    envelope_ = target + smoothCoeff_ * (envelope_ - target);
    if (envelope_ < 1e-5f) envelope_ = 0.0f;
    if (envelope_ > 0.99999f) envelope_ = 1.0f;

    return envelope_;
}

} // namespace openglitch::core
