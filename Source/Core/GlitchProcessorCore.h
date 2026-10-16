#pragma once

#include "BeatStepSequencer.h"
#include "GlitchEffects.h"
#include <array>
#include <cstdint>

namespace openglitch::core {

enum class GlitchFxId : int {
    // Phase 1: Initial 5 Glitch FX
    Bitcrush = 0,
    Downsample = 1,
    Stutter = 2,
    TapeStop = 3,
    RingMod = 4,

    // Phase 2: 5 Additional Glitch FX
    Reverse = 5,
    Chopper = 6,
    PitchGlitch = 7,
    CombReso = 8,
    Wavefold = 9,

    Count = 10
};

struct MeterTelemetry {
    float peakL = 0.0f;
    float peakR = 0.0f;
    float activeGate = 0.0f;
    int currentStep = 0;
    float barPhase = 0.0f;
    bool stepEnabled = true;
    uint16_t currentStepFxMask = BeatStepSequencer::kAllFxMask;
};

class GlitchProcessorCore {
public:
    static constexpr int kNumEffects = static_cast<int>(GlitchFxId::Count);

    GlitchProcessorCore();

    void prepare(double sampleRate);
    void reset();

    // 32-Step Sequencer access (global step enable + per-step 10-FX assignment bitmask)
    void setStep(int stepIndex, bool enabled);
    bool getStep(int stepIndex) const;
    void setStepFxMask(int stepIndex, uint16_t fxMask);
    uint16_t getStepFxMask(int stepIndex) const;
    void setStepFxEnabled(int stepIndex, GlitchFxId fxId, bool enabled);
    bool isStepFxEnabled(int stepIndex, GlitchFxId fxId) const;
    void setAllStepsFxMask(uint16_t fxMask);

    void setAllSteps(bool enabled);
    void setPatternBitmask(uint32_t mask);
    uint32_t getPatternBitmask() const;
    void applyPatternPreset(StepPatternPreset preset);

    // Transport / Tempo Sync
    void setTransport(double bpm, bool useHostSync, bool hasValidHostPpq, double hostPpqPosition);
    void setGateSmoothingMs(float ms);

    // Glitch FX parameters
    void setEffectEnabled(GlitchFxId id, bool enabled);
    bool isEffectEnabled(GlitchFxId id) const;
    void setEffectAmount(GlitchFxId id, float amount01);
    float getEffectAmount(GlitchFxId id) const;

    // Master parameters
    void setMasterMix(float mix01);
    float getMasterMix() const { return masterMix_; }
    void setMasterGainDb(float db);
    float getMasterGainDb() const { return masterGainDb_; }
    void setMasterBypassed(bool bypassed) { masterBypassed_ = bypassed; }
    bool isMasterBypassed() const { return masterBypassed_; }

    void processBlock(float* leftChannel, float* rightChannel, int numSamples);

    MeterTelemetry getTelemetry() const { return telemetry_; }

private:
    double sampleRate_ = 44100.0;

    BeatStepSequencer sequencer_;

    // Transport state for current block
    double bpm_ = 120.0;
    bool useHostSync_ = true;
    bool hasValidHostPpq_ = false;
    double blockStartPpq_ = 0.0;

    // Phase 1: 5 Glitch FX
    BitrateCrusher bitcrusher_;
    SampleRateDecimator decimator_;
    BufferStutter stutter_;
    TapeStopEffect tapeStop_;
    RingModEffect ringMod_;

    // Phase 2: 5 Additional Glitch FX (10 Total)
    ReverseGrainEffect reverseGrain_;
    ChopperGateEffect chopperGate_;
    PitchGlitchEffect pitchGlitch_;
    CombResonatorEffect combResonator_;
    WavefolderEffect wavefolder_;

    std::array<bool, kNumEffects> fxEnabled_{};
    std::array<float, kNumEffects> fxAmount_{};

    float masterMix_ = 1.0f;
    float masterGainDb_ = 0.0f;
    float masterGainLinear_ = 1.0f;
    bool masterBypassed_ = false;

    MeterTelemetry telemetry_{};
};

} // namespace openglitch::core
