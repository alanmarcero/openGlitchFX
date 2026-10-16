#pragma once

#include <array>
#include <cstdint>

namespace openglitch::core {

enum class StepPatternPreset {
    AllOn = 0,
    LastBeat,          // Steps 25..32 (Beat 4 of the bar)
    LastHalfBar,       // Steps 17..32 (Beats 3 & 4)
    LastFourSteps,     // Steps 29..32 (Last four 1/32 steps)
    Last32nd,          // Step 32 only (Last 1/32 of a bar)
    Backbeat2And4,     // Beats 2 & 4 (Steps 9..16 and 25..32)
    Stutter16ths,      // Every odd pair of 1/32s
    Every4thStep,      // Steps 4, 8, 12, 16, 20, 24, 28, 32
    ClearAll
};

class BeatStepSequencer {
public:
    static constexpr int kNumSteps = 32;
    static constexpr int kNumFxSlots = 10;
    static constexpr uint16_t kAllFxMask = (1u << kNumFxSlots) - 1u; // 0x03FF (all 10 FX enabled on pad)
    static constexpr double kQuarterNotesPerBar = 4.0; // 4/4 bar

    BeatStepSequencer();

    void prepare(double sampleRate);
    void reset();

    // Per-step master enable/disable
    void setStep(int stepIndex, bool enabled);
    bool getStep(int stepIndex) const;

    // Per-step FX assignment bitmask (bits 0..9 correspond to the 10 Glitch FX)
    void setStepFxMask(int stepIndex, uint16_t fxMask);
    uint16_t getStepFxMask(int stepIndex) const;
    void setStepFxEnabled(int stepIndex, int fxIndex, bool enabled);
    bool isStepFxEnabled(int stepIndex, int fxIndex) const;
    void setAllStepsFxMask(uint16_t fxMask);

    void setAllSteps(bool enabled);
    void setPatternBitmask(uint32_t mask);
    uint32_t getPatternBitmask() const;
    void applyPreset(StepPatternPreset preset);

    void setSmoothingTimeMs(float ms);
    float getSmoothingTimeMs() const { return smoothingTimeMs_; }

    // Advance by one audio sample and return the overall smoothed gate envelope [0.0f .. 1.0f].
    // Also updates per-FX smoothed envelopes and sets stepTransitionedToActive / fxTransitionedMask.
    float advanceSample(double bpm,
                        bool useHostSync,
                        bool hasValidHostPpq,
                        double hostPpqAtSample,
                        bool& stepTransitionedToActive);

    float advanceSampleWithFxMask(double bpm,
                                  bool useHostSync,
                                  bool hasValidHostPpq,
                                  double hostPpqAtSample,
                                  bool& stepTransitionedToActive,
                                  uint16_t& fxTransitionedToActiveMask);

    int getCurrentStep() const { return currentStep_; }
    double getCurrentBarPhase() const { return currentBarPhase_; }
    bool isCurrentStepEnabled() const;
    uint16_t getCurrentStepActiveFxMask() const;
    float getCurrentEnvelope() const { return envelope_; }
    float getFxEnvelope(int fxIndex) const;

    static int computeStepFromPpq(double ppqPosition, double* outBarPhase = nullptr);

private:
    void updateSmoothingCoeff();

    double sampleRate_ = 44100.0;
    double internalPpq_ = 0.0;
    std::array<bool, kNumSteps> steps_{};
    std::array<uint16_t, kNumSteps> stepFxMasks_{};

    int currentStep_ = 0;
    double currentBarPhase_ = 0.0;
    bool wasActiveStep_ = false;
    uint16_t wasActiveFxMask_ = 0u;

    float smoothingTimeMs_ = 2.5f;
    float smoothCoeff_ = 0.99f;
    float envelope_ = 0.0f;
    std::array<float, kNumFxSlots> fxEnvelopes_{};
};

} // namespace openglitch::core
