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
    wetFilter_.prepare(sampleRate_);

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
    wetFilter_.reset();
    heldNotes_.fill(0);
    midiQueueSize_ = 0;
    sequencer_.setExternalFxMask(0u);
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

            // 1. Time/Buffer FX (continuous across combined adjacent pads!)
            applyGatedFx(GlitchFxId::Stutter, [&](float amt, float& l, float& r) {
                StutterSettings stutter = stutterSettings_;
                stutter.rateMultiplier = std::pow(2.0, 2.0 * static_cast<double>(modulation(EnvTarget::Pitch, 0.5f)) - 1.0);
                stutter_.processSample(amt, bpm_, stutter, l, r);
            });
            applyGatedFx(GlitchFxId::Reverse, [&](float amt, float& l, float& r) {
                const auto span = sequencer_.getActiveFxSpan(curStep, static_cast<int>(GlitchFxId::Reverse));
                const double spanSec = static_cast<double>(span.lengthSteps) * secPer32nd;
                reverseGrain_.processSample(amt, span.spanPhase, spanSec, l, r);
            });
            applyGatedFx(GlitchFxId::TapeStop, [&](float amt, float& l, float& r) {
                const auto span = sequencer_.getActiveFxSpan(curStep, static_cast<int>(GlitchFxId::TapeStop));
                const double spanSec = tapeLengthBeats_ > 0.0 ? tapeLengthBeats_ * secPerBeat
                                                              : static_cast<double>(span.lengthSteps) * secPer32nd;
                TapeStopSettings tape = tapeStopSettings_;
                tape.fixedDurationSec = tapeLengthBeats_ > 0.0 ? spanSec : 0.0;
                tapeStop_.processSample(amt, spanSec, tape, l, r);
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

            // 4. Filter, volume and pan
            const float filterMod = modulation(EnvTarget::Filter, 1.0f);
            const float cutoff = filterCutoffHz_ * std::exp2(8.0f * (filterMod - 1.0f));
            wetFilter_.processSample(filterMode_, cutoff, filterResonance_, wetL, wetR);

            const float volume = modulation(EnvTarget::Volume, 1.0f);
            const float pan = 2.0f * modulation(EnvTarget::Pan, 0.5f) - 1.0f;
            wetL *= volume * std::min(1.0f, 1.0f - pan);
            wetR *= volume * std::min(1.0f, 1.0f + pan);

            const float dryGain = (mixMode_ == MixMode::Parallel) ? 1.0f : (1.0f - effectiveWet);
            outL = (dryL * dryGain + wetL * effectiveWet) * currentMasterGainLinear_;
            outR = (dryR * dryGain + wetR * effectiveWet) * currentMasterGainLinear_;
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
