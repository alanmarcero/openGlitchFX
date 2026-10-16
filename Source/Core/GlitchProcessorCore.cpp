#include "GlitchProcessorCore.h"
#include <algorithm>
#include <cmath>

namespace openglitch::core {

GlitchProcessorCore::GlitchProcessorCore() {
    fxEnabled_.fill(true);
    fxAmount_.fill(0.0f);
}

void GlitchProcessorCore::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    sequencer_.prepare(sampleRate_);
    bitcrusher_.prepare(sampleRate_);
    decimator_.prepare(sampleRate_);
    stutter_.prepare(sampleRate_);
    tapeStop_.prepare(sampleRate_);
    ringMod_.prepare(sampleRate_);
    reset();
}

void GlitchProcessorCore::reset() {
    sequencer_.reset();
    bitcrusher_.reset();
    decimator_.reset();
    stutter_.reset();
    tapeStop_.reset();
    ringMod_.reset();
}

void GlitchProcessorCore::setStep(int stepIndex, bool enabled) {
    sequencer_.setStep(stepIndex, enabled);
}

bool GlitchProcessorCore::getStep(int stepIndex) const {
    return sequencer_.getStep(stepIndex);
}

void GlitchProcessorCore::setAllSteps(bool enabled) {
    sequencer_.setAllSteps(enabled);
}

void GlitchProcessorCore::setPatternBitmask(uint32_t mask) {
    sequencer_.setPatternBitmask(mask);
}

uint32_t GlitchProcessorCore::getPatternBitmask() const {
    return sequencer_.getPatternBitmask();
}

void GlitchProcessorCore::applyPatternPreset(StepPatternPreset preset) {
    sequencer_.applyPreset(preset);
}

void GlitchProcessorCore::setTransport(double bpm, bool useHostSync, bool hasValidHostPpq, double hostPpqPosition) {
    bpm_ = std::clamp(bpm, 20.0, 300.0);
    useHostSync_ = useHostSync;
    hasValidHostPpq_ = hasValidHostPpq;
    blockStartPpq_ = hostPpqPosition;
}

void GlitchProcessorCore::setGateSmoothingMs(float ms) {
    sequencer_.setSmoothingTimeMs(ms);
}

void GlitchProcessorCore::setEffectEnabled(GlitchFxId id, bool enabled) {
    const int idx = static_cast<int>(id);
    if (idx >= 0 && idx < kNumEffects) {
        fxEnabled_[static_cast<size_t>(idx)] = enabled;
    }
}

bool GlitchProcessorCore::isEffectEnabled(GlitchFxId id) const {
    const int idx = static_cast<int>(id);
    if (idx >= 0 && idx < kNumEffects) {
        return fxEnabled_[static_cast<size_t>(idx)];
    }
    return false;
}

void GlitchProcessorCore::setEffectAmount(GlitchFxId id, float amount01) {
    const int idx = static_cast<int>(id);
    if (idx >= 0 && idx < kNumEffects) {
        fxAmount_[static_cast<size_t>(idx)] = std::clamp(amount01, 0.0f, 1.0f);
    }
}

float GlitchProcessorCore::getEffectAmount(GlitchFxId id) const {
    const int idx = static_cast<int>(id);
    if (idx >= 0 && idx < kNumEffects) {
        return fxAmount_[static_cast<size_t>(idx)];
    }
    return 0.0f;
}

void GlitchProcessorCore::setMasterMix(float mix01) {
    masterMix_ = std::clamp(mix01, 0.0f, 1.0f);
}

void GlitchProcessorCore::setMasterGainDb(float db) {
    masterGainDb_ = std::clamp(db, -24.0f, 24.0f);
    masterGainLinear_ = std::pow(10.0f, masterGainDb_ / 20.0f);
}

void GlitchProcessorCore::processBlock(float* leftChannel, float* rightChannel, int numSamples) {
    if (leftChannel == nullptr || numSamples <= 0) {
        return;
    }

    const bool isMono = (rightChannel == nullptr);
    const double ppqPerSample = (bpm_ / 60.0) / sampleRate_;

    float maxPeakL = 0.0f;
    float maxPeakR = 0.0f;
    float lastGate = 0.0f;

    for (int i = 0; i < numSamples; ++i) {
        const float dryL = leftChannel[i];
        const float dryR = isMono ? dryL : rightChannel[i];

        const double sampleHostPpq = blockStartPpq_ + static_cast<double>(i) * ppqPerSample;
        bool enteredActive = false;
        const float stepGate = sequencer_.advanceSample(bpm_, useHostSync_, hasValidHostPpq_, sampleHostPpq, enteredActive);
        lastGate = stepGate;

        if (masterBypassed_) {
            maxPeakL = std::max(maxPeakL, std::abs(dryL));
            maxPeakR = std::max(maxPeakR, std::abs(dryR));
            continue;
        }

        if (enteredActive) {
            stutter_.triggerCapture();
            tapeStop_.triggerBrake();
        }

        // Only run the glitch FX chain when the beat-synced step gate is open (> 0)
        float outL = dryL;
        float outR = dryR;

        const float effectiveWet = stepGate * masterMix_;
        if (effectiveWet > 1e-5f) {
            float wetL = dryL;
            float wetR = dryR;

            // 1. Stutter (Beat Repeat)
            if (fxEnabled_[static_cast<size_t>(GlitchFxId::Stutter)] &&
                fxAmount_[static_cast<size_t>(GlitchFxId::Stutter)] > 1e-4f) {
                stutter_.processSample(fxAmount_[static_cast<size_t>(GlitchFxId::Stutter)], bpm_, wetL, wetR);
            }

            // 2. Tape Stop
            if (fxEnabled_[static_cast<size_t>(GlitchFxId::TapeStop)] &&
                fxAmount_[static_cast<size_t>(GlitchFxId::TapeStop)] > 1e-4f) {
                tapeStop_.processSample(fxAmount_[static_cast<size_t>(GlitchFxId::TapeStop)], wetL, wetR);
            }

            // 3. Bitrate Crush
            if (fxEnabled_[static_cast<size_t>(GlitchFxId::Bitcrush)] &&
                fxAmount_[static_cast<size_t>(GlitchFxId::Bitcrush)] > 1e-4f) {
                bitcrusher_.processSample(fxAmount_[static_cast<size_t>(GlitchFxId::Bitcrush)], wetL, wetR);
            }

            // 4. Sample Rate Decimator
            if (fxEnabled_[static_cast<size_t>(GlitchFxId::Downsample)] &&
                fxAmount_[static_cast<size_t>(GlitchFxId::Downsample)] > 1e-4f) {
                decimator_.processSample(fxAmount_[static_cast<size_t>(GlitchFxId::Downsample)], wetL, wetR);
            }

            // 5. Ring Modulator
            if (fxEnabled_[static_cast<size_t>(GlitchFxId::RingMod)] &&
                fxAmount_[static_cast<size_t>(GlitchFxId::RingMod)] > 1e-4f) {
                ringMod_.processSample(fxAmount_[static_cast<size_t>(GlitchFxId::RingMod)], wetL, wetR);
            }

            // Crossfade dry & wet according to the 32-step beat gate envelope * master mix
            outL = (dryL * (1.0f - effectiveWet) + wetL * effectiveWet) * masterGainLinear_;
            outR = (dryR * (1.0f - effectiveWet) + wetR * effectiveWet) * masterGainLinear_;
        } else {
            outL = dryL * masterGainLinear_;
            outR = dryR * masterGainLinear_;
        }

        // Safety hard-clip protection at +-4.0 (+12 dBFS)
        outL = std::clamp(outL, -4.0f, 4.0f);
        outR = std::clamp(outR, -4.0f, 4.0f);

        leftChannel[i] = outL;
        if (!isMono) {
            rightChannel[i] = outR;
        }

        maxPeakL = std::max(maxPeakL, std::abs(outL));
        maxPeakR = std::max(maxPeakR, std::abs(outR));
    }

    if (!useHostSync_ || !hasValidHostPpq_) {
        blockStartPpq_ += static_cast<double>(numSamples) * ppqPerSample;
    }

    telemetry_.peakL = maxPeakL;
    telemetry_.peakR = maxPeakR;
    telemetry_.activeGate = lastGate;
    telemetry_.currentStep = sequencer_.getCurrentStep();
    telemetry_.barPhase = static_cast<float>(sequencer_.getCurrentBarPhase());
    telemetry_.stepEnabled = sequencer_.isCurrentStepEnabled();
}

} // namespace openglitch::core
