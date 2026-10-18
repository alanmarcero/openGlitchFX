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

    // Phase 1 FX
    bitcrusher_.prepare(sampleRate_);
    decimator_.prepare(sampleRate_);
    stutter_.prepare(sampleRate_);
    tapeStop_.prepare(sampleRate_);
    ringMod_.prepare(sampleRate_);

    // Phase 2 FX
    reverseGrain_.prepare(sampleRate_);
    chopperGate_.prepare(sampleRate_);
    pitchGlitch_.prepare(sampleRate_);
    combResonator_.prepare(sampleRate_);
    wavefolder_.prepare(sampleRate_);

    reset();
}

void GlitchProcessorCore::reset() {
    sequencer_.reset();
    bitcrusher_.reset();
    decimator_.reset();
    stutter_.reset();
    tapeStop_.reset();
    ringMod_.reset();
    reverseGrain_.reset();
    chopperGate_.reset();
    pitchGlitch_.reset();
    combResonator_.reset();
    wavefolder_.reset();
    currentMasterMix_ = masterMix_;
    currentMasterGainLinear_ = masterGainLinear_;
    hasProcessedAudio_ = false;
}

void GlitchProcessorCore::setStep(int stepIndex, bool enabled) {
    sequencer_.setStep(stepIndex, enabled);
}

bool GlitchProcessorCore::getStep(int stepIndex) const {
    return sequencer_.getStep(stepIndex);
}

void GlitchProcessorCore::setStepFxMask(int stepIndex, uint16_t fxMask) {
    sequencer_.setStepFxMask(stepIndex, fxMask);
}

uint16_t GlitchProcessorCore::getStepFxMask(int stepIndex) const {
    return sequencer_.getStepFxMask(stepIndex);
}

void GlitchProcessorCore::setStepFxEnabled(int stepIndex, GlitchFxId fxId, bool enabled) {
    sequencer_.setStepFxEnabled(stepIndex, static_cast<int>(fxId), enabled);
}

bool GlitchProcessorCore::isStepFxEnabled(int stepIndex, GlitchFxId fxId) const {
    return sequencer_.isStepFxEnabled(stepIndex, static_cast<int>(fxId));
}

void GlitchProcessorCore::setAllStepsFxMask(uint16_t fxMask) {
    sequencer_.setAllStepsFxMask(fxMask);
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
    if (!hasProcessedAudio_) {
        currentMasterMix_ = masterMix_;
    }
}

void GlitchProcessorCore::setMasterGainDb(float db) {
    masterGainDb_ = std::clamp(db, -24.0f, 24.0f);
    masterGainLinear_ = std::pow(10.0f, masterGainDb_ / 20.0f);
    if (!hasProcessedAudio_) {
        currentMasterGainLinear_ = masterGainLinear_;
    }
}

void GlitchProcessorCore::processBlock(float* leftChannel, float* rightChannel, int numSamples) {
    if (leftChannel == nullptr || numSamples <= 0) {
        return;
    }

    const bool isMono = (rightChannel == nullptr);
    const double ppqPerSample = (bpm_ / 60.0) / sampleRate_;
    const float mixStep = (masterMix_ - currentMasterMix_) / static_cast<float>(numSamples);
    const float gainStep = (masterGainLinear_ - currentMasterGainLinear_) / static_cast<float>(numSamples);

    float maxInPeakL = 0.0f;
    float maxInPeakR = 0.0f;
    float maxPeakL = 0.0f;
    float maxPeakR = 0.0f;
    float lastGate = 0.0f;

    auto softCeiling = [](float x) noexcept -> float {
        const float absX = std::abs(x);
        if (absX <= 1.25f) {
            return x;
        }
        const float sign = (x >= 0.0f) ? 1.0f : -1.0f;
        return sign * (1.25f + 0.75f * std::tanh((absX - 1.25f) / 0.75f));
    };

    for (int i = 0; i < numSamples; ++i) {
        currentMasterMix_ += mixStep;
        currentMasterGainLinear_ += gainStep;

        const float dryL = leftChannel[i];
        const float dryR = isMono ? dryL : rightChannel[i];
        maxInPeakL = std::max(maxInPeakL, std::abs(dryL));
        maxInPeakR = std::max(maxInPeakR, std::abs(dryR));

        // Keep circular lookback buffers warm with incoming audio on every sample
        reverseGrain_.recordDrySample(dryL, dryR);
        pitchGlitch_.recordDrySample(dryL, dryR);

        const double sampleHostPpq = blockStartPpq_ + static_cast<double>(i) * ppqPerSample;
        bool enteredActive = false;
        uint16_t fxTransitionMask = 0u;
        const float stepGate = sequencer_.advanceSampleWithFxMask(
            bpm_, useHostSync_, hasValidHostPpq_, sampleHostPpq, enteredActive, fxTransitionMask);
        lastGate = stepGate;

        if (masterBypassed_) {
            maxPeakL = std::max(maxPeakL, std::abs(dryL));
            maxPeakR = std::max(maxPeakR, std::abs(dryR));
            continue;
        }

        // Trigger time-buffered effects when their specific per-pad assignment transitions to active
        if ((fxTransitionMask & (1u << static_cast<int>(GlitchFxId::Stutter))) != 0u) {
            stutter_.triggerCapture();
        }
        if ((fxTransitionMask & (1u << static_cast<int>(GlitchFxId::TapeStop))) != 0u) {
            tapeStop_.triggerBrake();
        }
        if ((fxTransitionMask & (1u << static_cast<int>(GlitchFxId::Reverse))) != 0u) {
            reverseGrain_.triggerGrain();
        }

        float outL = dryL;
        float outR = dryR;

        const float effectiveWet = stepGate * currentMasterMix_;
        if (effectiveWet > 1e-5f) {
            float wetL = dryL;
            float wetR = dryR;

            // Helper: returns the per-pad smoothed FX envelope (0..1) if the FX module is enabled
            auto getFxGate = [&](GlitchFxId id) -> float {
                const auto idx = static_cast<size_t>(id);
                if (!fxEnabled_[idx] || fxAmount_[idx] <= 1e-4f) {
                    return 0.0f;
                }
                return sequencer_.getFxEnvelope(static_cast<int>(id));
            };

            auto applyGatedFx = [&](GlitchFxId id, auto&& processFn) {
                const float g = getFxGate(id);
                if (g > 1e-5f) {
                    float fxL = wetL;
                    float fxR = wetR;
                    processFn(fxAmount_[static_cast<size_t>(id)], fxL, fxR);
                    wetL = wetL * (1.0f - g) + fxL * g;
                    wetR = wetR * (1.0f - g) + fxR * g;
                }
            };

            const int curStep = sequencer_.getCurrentStep();
            const double secPer32nd = (60.0 / bpm_) * (4.0 / 32.0); // 7.5 / bpm_

            // 1. Time/Buffer FX (continuous across combined adjacent pads!)
            applyGatedFx(GlitchFxId::Stutter, [&](float amt, float& l, float& r) {
                stutter_.processSample(amt, bpm_, l, r);
            });
            applyGatedFx(GlitchFxId::Reverse, [&](float amt, float& l, float& r) {
                const auto span = sequencer_.getActiveFxSpan(curStep, static_cast<int>(GlitchFxId::Reverse));
                const double spanSec = static_cast<double>(span.lengthSteps) * secPer32nd;
                reverseGrain_.processSample(amt, span.spanPhase, spanSec, l, r);
            });
            applyGatedFx(GlitchFxId::TapeStop, [&](float amt, float& l, float& r) {
                const auto span = sequencer_.getActiveFxSpan(curStep, static_cast<int>(GlitchFxId::TapeStop));
                const double spanSec = static_cast<double>(span.lengthSteps) * secPer32nd;
                tapeStop_.processSample(amt, spanSec, l, r);
            });
            applyGatedFx(GlitchFxId::PitchGlitch, [&](float amt, float& l, float& r) {
                pitchGlitch_.processSample(amt, l, r);
            });

            // 2. Harmonic / Spectral / Bitrate FX
            applyGatedFx(GlitchFxId::Bitcrush, [&](float amt, float& l, float& r) {
                bitcrusher_.processSample(amt, l, r);
            });
            applyGatedFx(GlitchFxId::Downsample, [&](float amt, float& l, float& r) {
                decimator_.processSample(amt, l, r);
            });
            applyGatedFx(GlitchFxId::Wavefold, [&](float amt, float& l, float& r) {
                wavefolder_.processSample(amt, l, r);
            });
            applyGatedFx(GlitchFxId::RingMod, [&](float amt, float& l, float& r) {
                ringMod_.processSample(amt, l, r);
            });
            applyGatedFx(GlitchFxId::CombReso, [&](float amt, float& l, float& r) {
                combResonator_.processSample(amt, l, r);
            });

            // 3. Rhythmic Chopper Slicer (sustains across combined pad spans)
            applyGatedFx(GlitchFxId::Chopper, [&](float amt, float& l, float& r) {
                const auto span = sequencer_.getActiveFxSpan(curStep, static_cast<int>(GlitchFxId::Chopper));
                chopperGate_.processSample(amt, bpm_, span.lengthSteps, l, r);
            });

            outL = (dryL * (1.0f - effectiveWet) + wetL * effectiveWet) * currentMasterGainLinear_;
            outR = (dryR * (1.0f - effectiveWet) + wetR * effectiveWet) * currentMasterGainLinear_;
        } else {
            outL = dryL * currentMasterGainLinear_;
            outR = dryR * currentMasterGainLinear_;
        }

        outL = softCeiling(outL);
        outR = softCeiling(outR);

        leftChannel[i] = outL;
        if (!isMono) {
            rightChannel[i] = outR;
        }

        maxPeakL = std::max(maxPeakL, std::abs(outL));
        maxPeakR = std::max(maxPeakR, std::abs(outR));
    }

    currentMasterMix_ = masterMix_;
    currentMasterGainLinear_ = masterGainLinear_;
    hasProcessedAudio_ = true;

    if (!useHostSync_ || !hasValidHostPpq_) {
        blockStartPpq_ += static_cast<double>(numSamples) * ppqPerSample;
    }

    const float decay = std::exp(-static_cast<float>(numSamples) / (0.22f * static_cast<float>(sampleRate_)));
    telemetry_.inputPeakL = std::max(maxInPeakL, telemetry_.inputPeakL * decay);
    telemetry_.inputPeakR = std::max(maxInPeakR, telemetry_.inputPeakR * decay);
    telemetry_.peakL = std::max(maxPeakL, telemetry_.peakL * decay);
    telemetry_.peakR = std::max(maxPeakR, telemetry_.peakR * decay);
    telemetry_.clipOut = (maxPeakL >= 0.999f || maxPeakR >= 0.999f);
    telemetry_.activeGate = lastGate;
    telemetry_.currentStep = sequencer_.getCurrentStep();
    telemetry_.barPhase = static_cast<float>(sequencer_.getCurrentBarPhase());
    telemetry_.stepEnabled = sequencer_.isCurrentStepEnabled();
    telemetry_.currentStepFxMask = sequencer_.getCurrentStepActiveFxMask();
    for (int fx = 0; fx < BeatStepSequencer::kNumFxSlots; ++fx) {
        const auto idx = static_cast<size_t>(fx);
        const bool armed = fxEnabled_[idx] && (fxAmount_[idx] > 1e-4f);
        telemetry_.fxEnvelopes[idx] = armed ? (sequencer_.getFxEnvelope(fx) * fxAmount_[idx]) : 0.0f;
    }
}

} // namespace openglitch::core
