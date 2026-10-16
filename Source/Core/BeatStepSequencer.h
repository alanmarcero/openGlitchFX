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
    static constexpr double kQuarterNotesPerBar = 4.0; // 4/4 bar

    BeatStepSequencer();

    void prepare(double sampleRate);
    void reset();

    void setStep(int stepIndex, bool enabled);
    bool getStep(int stepIndex) const;

    void setAllSteps(bool enabled);
    void setPatternBitmask(uint32_t mask);
    uint32_t getPatternBitmask() const;
    void applyPreset(StepPatternPreset preset);

    void setSmoothingTimeMs(float ms);
    float getSmoothingTimeMs() const { return smoothingTimeMs_; }

    // Advance by one audio sample and return the smoothed gate envelope [0.0f .. 1.0f]
    // Also sets stepTransitionedToActive = true on the exact sample a disabled->enabled step boundary is crossed.
    float advanceSample(double bpm,
                        bool useHostSync,
                        bool hasValidHostPpq,
                        double hostPpqAtSample,
                        bool& stepTransitionedToActive);

    int getCurrentStep() const { return currentStep_; }
    double getCurrentBarPhase() const { return currentBarPhase_; }
    bool isCurrentStepEnabled() const;
    float getCurrentEnvelope() const { return envelope_; }

    static int computeStepFromPpq(double ppqPosition, double* outBarPhase = nullptr);

private:
    void updateSmoothingCoeff();

    double sampleRate_ = 44100.0;
    double internalPpq_ = 0.0;
    std::array<bool, kNumSteps> steps_{};

    int currentStep_ = 0;
    double currentBarPhase_ = 0.0;
    bool wasActiveStep_ = false;

    float smoothingTimeMs_ = 2.5f;
    float smoothCoeff_ = 0.99f;
    float envelope_ = 0.0f;
};

} // namespace openglitch::core
