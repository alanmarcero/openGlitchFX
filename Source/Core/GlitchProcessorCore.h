#pragma once

#include "BeatStepSequencer.h"
#include "GlitchEffects.h"
#include <array>
#include <atomic>

namespace openglitch::core {

enum class GlitchFxId : int {
    Bitcrush = 0,
    Downsample = 1,
    Stutter = 2,
    TapeStop = 3,
    RingMod = 4,
    Count = 5
};

struct MeterTelemetry {
    float peakL = 0.0f;
    float peakR = 0.0f;
    float activeGate = 0.0f;
    int currentStep = 0;
    float barPhase = 0.0f;
    bool stepEnabled = true;
};

class GlitchProcessorCore {
public:
    static constexpr int kNumEffects = static_cast<int>(GlitchFxId::Count);

    GlitchProcessorCore();

    void prepare(double sampleRate);
    void reset();

    // 32-Step Sequencer access
    void setStep(int stepIndex, bool enabled);
    bool getStep(int stepIndex) const;
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

    // 5 Glitch FX instances (Phase 1)
    BitrateCrusher bitcrusher_;
    SampleRateDecimator decimator_;
    BufferStutter stutter_;
    TapeStopEffect tapeStop_;
    RingModEffect ringMod_;

    std::array<bool, kNumEffects> fxEnabled_{};
    std::array<float, kNumEffects> fxAmount_{};

    float masterMix_ = 1.0f;
    float masterGainDb_ = 0.0f;
    float masterGainLinear_ = 1.0f;
    bool masterBypassed_ = false;

    MeterTelemetry telemetry_{};
};

} // namespace openglitch::core
