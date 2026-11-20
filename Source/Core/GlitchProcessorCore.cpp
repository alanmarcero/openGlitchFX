#include "GlitchProcessorCore.h"
#include <algorithm>
#include <cmath>
#include <numbers>

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
    chorus_.prepare(sampleRate_);
    flanger_.prepare(sampleRate_);
    phaser_.prepare(sampleRate_);
    pingPong_.prepare(sampleRate_);
    saturation_.prepare(sampleRate_);
    wetFilter_.prepare(sampleRate_);

    constexpr double kLr4Damp = 1.4142135623730951;
    const double gSub = std::tan(std::numbers::pi * std::min(95.0, sampleRate_ * 0.25) / sampleRate_);
    subLr4A1_ = static_cast<float>(1.0 / (1.0 + gSub * (gSub + kLr4Damp)));
    subLr4A2_ = static_cast<float>(gSub) * subLr4A1_;
    subLr4A3_ = static_cast<float>(gSub) * subLr4A2_;
    rmsAlpha_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.008 * sampleRate_)));
    gainAlpha_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.004 * sampleRate_)));

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
    reverseSpanSec_ = 0.0;
    reverseSpanPhase_ = 0.0f;
    chopperGate_.reset();
    pitchGlitch_.reset();
    combResonator_.reset();
    wavefolder_.reset();
    chorus_.reset();
    flanger_.reset();
    phaser_.reset();
    pingPong_.reset();
    saturation_.reset();
    wetFilter_.reset();
    heldNotes_.fill(0);
    midiQueueSize_ = 0;
    sequencer_.setExternalFxMask(0u);
    currentMasterMix_ = masterMix_;
    currentMasterGainLinear_ = masterGainLinear_;
    drySubLpIc1_ = {};
    drySubLpIc2_ = {};
    drySubHpIc1_ = {};
    drySubHpIc2_ = {};
    wetSubHpIc1_ = {};
    wetSubHpIc2_ = {};
    guardWavefold_ = {};
    guardComb_ = {};
    guardSaturation_ = {};
    guardMultiFx_ = {};
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

void GlitchProcessorCore::setSequenceLength(int steps) {
    sequencer_.setSequenceLength(steps);
}

void GlitchProcessorCore::setSequencerSwing(float percent) {
    sequencer_.setSwing(percent);
}

void GlitchProcessorCore::setPatternRate(float multiplier) {
    sequencer_.setPatternRate(multiplier);
}

void GlitchProcessorCore::setPatternChance(float percent) {
    sequencer_.setPatternChance(percent);
}

void GlitchProcessorCore::setTriggerMode(TriggerMode mode, bool syncedStart) {
    if (mode != triggerMode_) {
        heldNotes_.fill(0);
    }
    triggerMode_ = mode;
    sequencer_.setExternalTriggerMode(mode == TriggerMode::Midi, syncedStart);
}

void GlitchProcessorCore::queueMidiNote(int sampleOffset, int noteNumber, bool isNoteOn) {
    if (midiQueueSize_ >= kMaxQueuedMidi || noteNumber < 0 || noteNumber >= 128) {
        return;
    }
    midiQueue_[static_cast<size_t>(midiQueueSize_++)] = { sampleOffset, noteNumber, isNoteOn };
}

void GlitchProcessorCore::applyMidiTrigger(const MidiTrigger& trigger) {
    auto& held = heldNotes_[static_cast<size_t>(trigger.noteNumber)];
    held = trigger.isNoteOn ? static_cast<uint8_t>(std::min(255, held + 1)) : static_cast<uint8_t>(std::max(0, held - 1));

    uint16_t mask = 0u;
    for (int note = 0; note < 128; ++note) {
        if (heldNotes_[static_cast<size_t>(note)] == 0) {
            continue;
        }
        const int soloFx = note - kMidiSoloBaseNote;
        const bool isSoloNote = soloFx >= 0 && soloFx < kNumEffects;
        mask |= isSoloNote ? static_cast<uint16_t>(1u << soloFx) : BeatStepSequencer::kAllFxMask;
    }
    sequencer_.setExternalFxMask(mask);
}

void GlitchProcessorCore::setTapeStopSettings(const TapeStopSettings& settings, double lengthBeats) {
    tapeStopSettings_ = settings;
    tapeLengthBeats_ = std::max(0.0, lengthBeats);
}

void GlitchProcessorCore::setFilter(FilterMode mode, float cutoffHz, float resonance01) {
    filterMode_ = mode;
    filterCutoffHz_ = std::clamp(cutoffHz, 20.0f, 20000.0f);
    filterResonance_ = std::clamp(resonance01, 0.0f, 1.0f);
}

void GlitchProcessorCore::setEnvelopeSettings(EnvTarget target, const ModEnvelopeSettings& settings) {
    const int idx = static_cast<int>(target);
    if (idx >= 0 && idx < kNumEnvelopes) {
        envelopeSettings_[static_cast<size_t>(idx)] = settings;
    }
}

void GlitchProcessorCore::setEnvelopePoints(EnvTarget target, std::vector<EnvelopePoint> points) {
    const int idx = static_cast<int>(target);
    if (idx >= 0 && idx < kNumEnvelopes) {
        envelopes_[static_cast<size_t>(idx)].setPoints(std::move(points));
    }
}

float GlitchProcessorCore::envelopePhase(EnvTarget target, float spanPhase, double ppq) const {
    const double lengthBeats = envelopeSettings_[static_cast<size_t>(target)].lengthBeats;
    if (lengthBeats <= 0.0) {
        return spanPhase;
    }
    const double wrapped = std::fmod(ppq, lengthBeats);
    return static_cast<float>((wrapped < 0.0 ? wrapped + lengthBeats : wrapped) / lengthBeats);
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
    float lastSpanPhase = 0.0f;
    double lastPpq = blockStartPpq_;
    int midiIndex = 0;

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

        while (midiIndex < midiQueueSize_ && midiQueue_[static_cast<size_t>(midiIndex)].sampleOffset <= i) {
            applyMidiTrigger(midiQueue_[static_cast<size_t>(midiIndex++)]);
        }

        const double sampleHostPpq = blockStartPpq_ + static_cast<double>(i) * ppqPerSample;
        lastPpq = sampleHostPpq;
        bool enteredActive = false;
        uint16_t fxTransitionMask = 0u;
        const float stepGate = sequencer_.advanceSampleWithFxMask(
            bpm_, useHostSync_, hasValidHostPpq_, sampleHostPpq, enteredActive, fxTransitionMask);
        lastGate = stepGate;

        const bool echoArmed = fxEnabled_[static_cast<size_t>(GlitchFxId::PingPong)]
                            && fxAmount_[static_cast<size_t>(GlitchFxId::PingPong)] > 1e-4f;
        const bool combArmed = fxEnabled_[static_cast<size_t>(GlitchFxId::CombReso)]
                            && fxAmount_[static_cast<size_t>(GlitchFxId::CombReso)] > 1e-4f;

        if (masterBypassed_) {
            tapeStop_.recordDrySample(dryL, dryR);
            if (echoArmed) {
                float silentL = 0.0f;
                float silentR = 0.0f;
                pingPong_.processSendSample(fxAmount_[static_cast<size_t>(GlitchFxId::PingPong)], bpm_, 0.0f, silentL, silentR);
            }
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

        auto applyLoudnessGuard = [&](LoudnessGuard& guard, float preL, float preR, float& postL, float& postR) {
            const float kwInL = preL + 0.6f * (preL - guard.prevInL);
            const float kwInR = preR + 0.6f * (preR - guard.prevInR);
            guard.prevInL = preL;
            guard.prevInR = preR;

            const float kwOutL = postL + 0.6f * (postL - guard.prevOutL);
            const float kwOutR = postR + 0.6f * (postR - guard.prevOutR);
            guard.prevOutL = postL;
            guard.prevOutR = postR;

            const float inSq = 0.5f * (kwInL * kwInL + kwInR * kwInR);
            const float outSq = 0.5f * (kwOutL * kwOutL + kwOutR * kwOutR);
            guard.inPower += rmsAlpha_ * (inSq - guard.inPower);
            guard.outPower += rmsAlpha_ * (outSq - guard.outPower);

            if (guard.outPower > 1e-7f && guard.inPower > 1e-8f) {
                const float ratio = (guard.inPower + 1e-7f) / (guard.outPower + 1e-7f);
                const float targetGain = std::clamp(std::sqrt(ratio), 0.10f, 1.30f);
                guard.gain += gainAlpha_ * (targetGain - guard.gain);
            }
            postL *= guard.gain;
            postR *= guard.gain;
        };

        bool tapeRecorded = false;
        bool combProcessed = false;
        float wetL = dryL;
        float wetR = dryR;
        float sendTailL = 0.0f;
        float sendTailR = 0.0f;
        const float effectiveWet = stepGate * currentMasterMix_;

        if (effectiveWet > 1e-5f) {
            auto getFxGate = [&](GlitchFxId id) -> float {
                const auto idx = static_cast<size_t>(id);
                if (!fxEnabled_[idx] || fxAmount_[idx] <= 1e-4f) {
                    return 0.0f;
                }
                return sequencer_.getFxEnvelope(static_cast<int>(id));
            };

            // Buffer splices blend with equal-power crossfading over their S-curve micro-envelopes
            float maxTimeGate = 0.0f;
            auto applySpliceFx = [&](GlitchFxId id, float& busL, float& busR, auto&& processFn) {
                const float g = getFxGate(id);
                if (g > 1e-5f) {
                    maxTimeGate = std::max(maxTimeGate, g);
                    float fxL = busL;
                    float fxR = busR;
                    processFn(fxAmount_[static_cast<size_t>(id)], fxL, fxR);
                    const float theta = 0.5f * std::numbers::pi_v<float> * g;
                    const float dryCoeff = std::cos(theta);
                    const float wetCoeff = std::sin(theta);
                    busL = busL * dryCoeff + fxL * wetCoeff;
                    busR = busR * dryCoeff + fxR * wetCoeff;
                }
            };

            float maxSpecGate = 0.0f;
            auto applyGatedFx = [&](GlitchFxId id, float& busL, float& busR, bool& wasActive, auto&& processFn) {
                const float g = getFxGate(id);
                wasActive = g > 1e-5f;
                if (wasActive) {
                    maxSpecGate = std::max(maxSpecGate, g);
                    float fxL = busL;
                    float fxR = busR;
                    processFn(fxAmount_[static_cast<size_t>(id)], fxL, fxR);
                    busL = busL * (1.0f - g) + fxL * g;
                    busR = busR * (1.0f - g) + fxR * g;
                }
            };

            const int curStep = sequencer_.getCurrentStep();
            const double secPer32nd = (60.0 / bpm_) * (4.0 / 32.0);
            const double secPerBeat = 60.0 / bpm_;

            const float spanPhase = sequencer_.getMergedPadSpan(curStep).spanPhase;
            lastSpanPhase = spanPhase;
            auto modulation = [&](EnvTarget target, float neutral) {
                const auto& settings = envelopeSettings_[static_cast<size_t>(target)];
                if (!settings.enabled) {
                    return neutral;
                }
                const float y = envelopes_[static_cast<size_t>(target)].valueAt(envelopePhase(target, spanPhase, sampleHostPpq));
                return neutral + (y - neutral) * settings.depth;
            };

            // 1. Time/Buffer Bus (continuous across combined adjacent pads)
            float timeL = dryL;
            float timeR = dryR;
            applySpliceFx(GlitchFxId::Stutter, timeL, timeR, [&](float amt, float& l, float& r) {
                StutterSettings stutter = stutterSettings_;
                stutter.rateMultiplier = std::pow(2.0, 2.0 * static_cast<double>(modulation(EnvTarget::Pitch, 0.5f)) - 1.0);
                stutter_.processSample(amt, bpm_, stutter, l, r);
            });
            applySpliceFx(GlitchFxId::Reverse, timeL, timeR, [&](float amt, float& l, float& r) {
                const auto span = sequencer_.getActiveFxSpan(curStep, static_cast<int>(GlitchFxId::Reverse));
                if ((sequencer_.getCurrentStepActiveFxMask() & (1u << static_cast<int>(GlitchFxId::Reverse))) != 0u) {
                    reverseSpanSec_ = static_cast<double>(span.lengthSteps) * secPer32nd;
                    reverseSpanPhase_ = span.spanPhase;
                }
                reverseGrain_.processSample(amt, reverseSpanPhase_, reverseSpanSec_, l, r);
            });
            applySpliceFx(GlitchFxId::TapeStop, timeL, timeR, [&](float amt, float& l, float& r) {
                const auto span = sequencer_.getActiveFxSpan(curStep, static_cast<int>(GlitchFxId::TapeStop));
                const double spanSec = tapeLengthBeats_ > 0.0 ? tapeLengthBeats_ * secPerBeat
                                                              : static_cast<double>(span.lengthSteps) * secPer32nd;
                TapeStopSettings tape = tapeStopSettings_;
                tape.fixedDurationSec = tapeLengthBeats_ > 0.0 ? spanSec : 0.0;
                tapeStop_.processSample(amt, spanSec, tape, l, r);
                tapeRecorded = true;
            });
            applySpliceFx(GlitchFxId::PitchGlitch, timeL, timeR, [&](float amt, float& l, float& r) {
                pitchGlitch_.processSample(amt, l, r);
            });
            const float gChopper = getFxGate(GlitchFxId::Chopper);
            if (gChopper > 1e-5f) {
                maxTimeGate = std::max(maxTimeGate, gChopper);
                float chopL = timeL;
                float chopR = timeR;
                const auto span = sequencer_.getActiveFxSpan(curStep, static_cast<int>(GlitchFxId::Chopper));
                chopperGate_.processSample(fxAmount_[static_cast<size_t>(GlitchFxId::Chopper)], bpm_, span.lengthSteps, chopL, chopR);
                timeL = timeL * (1.0f - gChopper) + chopL * gChopper;
                timeR = timeR * (1.0f - gChopper) + chopR * gChopper;
            }

            // 2. Spectral / Nonlinear / Modulation Bus (Series or ParallelBuses routing)
            const bool parallelRouting = (fxRoutingMode_ == FxRoutingMode::ParallelBuses);
            const float specInL = parallelRouting ? dryL : timeL;
            const float specInR = parallelRouting ? dryR : timeR;
            float specL = specInL;
            float specR = specInR;
            int activeNonlinearCount = 0;
            bool stageActive = false;

            applyGatedFx(GlitchFxId::Bitcrush, specL, specR, stageActive, [&](float amt, float& l, float& r) {
                bitcrusher_.processSample(amt, l, r);
            });
            if (stageActive) ++activeNonlinearCount;

            applyGatedFx(GlitchFxId::Downsample, specL, specR, stageActive, [&](float amt, float& l, float& r) {
                decimator_.processSample(amt, l, r);
            });
            if (stageActive) ++activeNonlinearCount;

            applyGatedFx(GlitchFxId::Wavefold, specL, specR, stageActive, [&](float amt, float& l, float& r) {
                const float preL = l;
                const float preR = r;
                wavefolder_.processSample(amt, l, r);
                applyLoudnessGuard(guardWavefold_, preL, preR, l, r);
            });
            if (stageActive) ++activeNonlinearCount;

            applyGatedFx(GlitchFxId::RingMod, specL, specR, stageActive, [&](float amt, float& l, float& r) {
                ringMod_.processSample(amt, l, r);
            });
            if (stageActive) ++activeNonlinearCount;

            // Tail-preserving post-fader send gating on CombReso with nonlinear RMS compensation
            float gComb = 0.0f;
            if (combArmed) gComb = sequencer_.getFxEnvelope(static_cast<int>(GlitchFxId::CombReso));
            if (combArmed && (gComb > 1e-5f || combResonator_.hasAudibleTail())) {
                const float preL = specL;
                const float preR = specR;
                combResonator_.processSendSample(fxAmount_[static_cast<size_t>(GlitchFxId::CombReso)], gComb, specL, specR);
                if (gComb > 1e-5f) {
                    applyLoudnessGuard(guardComb_, preL, preR, specL, specR);
                    maxSpecGate = std::max(maxSpecGate, gComb);
                    ++activeNonlinearCount;
                }
                combProcessed = true;
            }

            applyGatedFx(GlitchFxId::Chorus, specL, specR, stageActive, [&](float amt, float& l, float& r) {
                chorus_.processSample(ModulatedDelayEffect::Mode::Chorus, amt, l, r);
            });
            applyGatedFx(GlitchFxId::Flanger, specL, specR, stageActive, [&](float amt, float& l, float& r) {
                flanger_.processSample(ModulatedDelayEffect::Mode::Flanger, amt, l, r);
            });
            applyGatedFx(GlitchFxId::Phaser, specL, specR, stageActive, [&](float amt, float& l, float& r) {
                phaser_.processSample(amt, l, r);
            });

            applyGatedFx(GlitchFxId::Saturation, specL, specR, stageActive, [&](float amt, float& l, float& r) {
                const float preL = l;
                const float preR = r;
                saturation_.processSample(amt, l, r);
                applyLoudnessGuard(guardSaturation_, preL, preR, l, r);
            });
            if (stageActive) ++activeNonlinearCount;

            if (activeNonlinearCount >= 2) {
                applyLoudnessGuard(guardMultiFx_, specInL, specInR, specL, specR);
            }

            wetL = specL;
            wetR = specR;
            if (parallelRouting) {
                const bool hasTime = maxTimeGate > 1e-5f;
                const bool hasSpec = maxSpecGate > 1e-5f;
                if (hasTime && hasSpec) {
                    constexpr float kEqualPowerBusGain = 0.70710678f;
                    wetL = (timeL + specL) * kEqualPowerBusGain;
                    wetR = (timeR + specR) * kEqualPowerBusGain;
                }
                if (hasTime && !hasSpec) {
                    wetL = timeL;
                    wetR = timeR;
                }
            }

            // 3. Filter, volume and pan
            if (filterMode_ != FilterMode::Off) {
                const float filterMod = modulation(EnvTarget::Filter, 1.0f);
                const float cutoff = filterCutoffHz_ * std::exp2(8.0f * (filterMod - 1.0f));
                wetFilter_.processSample(filterMode_, cutoff, filterResonance_, wetL, wetR);
            }

            const float volume = modulation(EnvTarget::Volume, 1.0f);
            const float pan = 2.0f * modulation(EnvTarget::Pan, 0.5f) - 1.0f;
            wetL *= volume * std::min(1.0f, 1.0f - pan);
            wetR *= volume * std::min(1.0f, 1.0f + pan);
        }

        if (!tapeRecorded) {
            tapeStop_.recordDrySample(dryL, dryR);
        }

        if (!combProcessed && combArmed && combResonator_.hasAudibleTail()) {
            float combTailL = 0.0f;
            float combTailR = 0.0f;
            combResonator_.processSendSample(fxAmount_[static_cast<size_t>(GlitchFxId::CombReso)], 0.0f, combTailL, combTailR);
            sendTailL += combTailL * guardComb_.gain;
            sendTailR += combTailR * guardComb_.gain;
        }

        // 4. Tail-preserving post-fader send gating on PingPongEcho
        float totalWetL = (effectiveWet > 1e-5f ? wetL * stepGate : 0.0f) + sendTailL;
        float totalWetR = (effectiveWet > 1e-5f ? wetR * stepGate : 0.0f) + sendTailR;
        if (echoArmed) {
            const float gEcho = sequencer_.getFxEnvelope(static_cast<int>(GlitchFxId::PingPong));
            pingPong_.processSendSample(fxAmount_[static_cast<size_t>(GlitchFxId::PingPong)], bpm_, gEcho, totalWetL, totalWetR);
        }

        // 5. Phase-complementary 24 dB/oct Linkwitz-Riley (LR4) Keep Sub crossover
        float outL = 0.0f;
        float outR = 0.0f;
        if (keepSubEnabled_) {
            constexpr float kLr4Damp = 1.41421356f;
            auto tickLr4Lp = [&](float x, size_t ch) noexcept {
                float sig = x;
                for (size_t st = 0; st < 2; ++st) {
                    const float v3 = sig - drySubLpIc2_[ch][st];
                    const float v1 = subLr4A1_ * drySubLpIc1_[ch][st] + subLr4A2_ * v3;
                    const float v2 = drySubLpIc2_[ch][st] + subLr4A2_ * drySubLpIc1_[ch][st] + subLr4A3_ * v3;
                    drySubLpIc1_[ch][st] = 2.0f * v1 - drySubLpIc1_[ch][st];
                    drySubLpIc2_[ch][st] = 2.0f * v2 - drySubLpIc2_[ch][st];
                    sig = v2;
                }
                return sig;
            };
            auto tickLr4Hp = [&](float x, std::array<std::array<float, 2>, 2>& ic1, std::array<std::array<float, 2>, 2>& ic2, size_t ch) noexcept {
                float sig = x;
                for (size_t st = 0; st < 2; ++st) {
                    const float v3 = sig - ic2[ch][st];
                    const float v1 = subLr4A1_ * ic1[ch][st] + subLr4A2_ * v3;
                    const float v2 = ic2[ch][st] + subLr4A2_ * ic1[ch][st] + subLr4A3_ * v3;
                    ic1[ch][st] = 2.0f * v1 - ic1[ch][st];
                    ic2[ch][st] = 2.0f * v2 - ic2[ch][st];
                    sig = sig - kLr4Damp * v1 - v2;
                }
                return sig;
            };

            const float dryLpL = tickLr4Lp(dryL, 0);
            const float dryLpR = tickLr4Lp(dryR, 1);
            const float dryHpL = tickLr4Hp(dryL, drySubHpIc1_, drySubHpIc2_, 0);
            const float dryHpR = tickLr4Hp(dryR, drySubHpIc1_, drySubHpIc2_, 1);
            const float wetHpL = tickLr4Hp(totalWetL, wetSubHpIc1_, wetSubHpIc2_, 0);
            const float wetHpR = tickLr4Hp(totalWetR, wetSubHpIc1_, wetSubHpIc2_, 1);

            const bool parallelMix = (mixMode_ == MixMode::Parallel);
            const float highDryGain = parallelMix ? 1.0f : (1.0f - effectiveWet);
            outL = (dryLpL + dryHpL * highDryGain + wetHpL * currentMasterMix_) * currentMasterGainLinear_;
            outR = (dryLpR + dryHpR * highDryGain + wetHpR * currentMasterMix_) * currentMasterGainLinear_;
        }
        if (!keepSubEnabled_) {
            const float dryGain = (mixMode_ == MixMode::Parallel) ? 1.0f : (1.0f - effectiveWet);
            outL = (dryL * dryGain + totalWetL * currentMasterMix_) * currentMasterGainLinear_;
            outR = (dryR * dryGain + totalWetR * currentMasterMix_) * currentMasterGainLinear_;
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

    while (midiIndex < midiQueueSize_) {
        applyMidiTrigger(midiQueue_[static_cast<size_t>(midiIndex++)]);
    }
    midiQueueSize_ = 0;

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
    for (int env = 0; env < kNumEnvelopes; ++env) {
        telemetry_.modEnvelopePhases[static_cast<size_t>(env)] = envelopePhase(static_cast<EnvTarget>(env), lastSpanPhase, lastPpq);
    }
}

} // namespace openglitch::core
