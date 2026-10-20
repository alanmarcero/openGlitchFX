#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace openglitch::core {

// 1. Bitrate Glitch Effect (Bit-depth & packet bitrate quantization controlled by a knob)
class BitrateCrusher {
public:
    void prepare(double sampleRate);
    void reset();
    void processSample(float amount, float& left, float& right);

private:
    double sampleRate_ = 44100.0;
    int frameCounter_ = 0;
    float heldL_ = 0.0f;
    float heldR_ = 0.0f;
    float smoothL_ = 0.0f;
    float smoothR_ = 0.0f;
};

// 2. Sample Rate Decimator (Sample-and-hold clock divider controlled by a knob)
class SampleRateDecimator {
public:
    void prepare(double sampleRate);
    void reset();
    void processSample(float amount, float& left, float& right);

private:
    double sampleRate_ = 44100.0;
    double phase_ = 0.0;
    float heldL_ = 0.0f;
    float heldR_ = 0.0f;
    float prevHeldL_ = 0.0f;
    float prevHeldR_ = 0.0f;
};

// 3. Buffer Stutter / Beat Repeat (Micro-slice loop repeater controlled by a knob)
enum class StutterReverse : int { Off = 0, All, Alternate };

struct StutterSettings {
    double syncedSliceBeats = 0.0; // 0 = the amount knob sets the slice
    bool advance = false;          // each repeat starts half a slice later
    StutterReverse reverse = StutterReverse::Off;
    float gate = 1.0f;             // audible part of each repeat
    float pitchStepSemitones = 0.0f;
    double rateMultiplier = 1.0;   // the pitch envelope sets this
};

class BufferStutter {
public:
    void prepare(double sampleRate);
    void reset();
    void triggerCapture();
    void processSample(float amount, double bpm, const StutterSettings& settings, float& left, float& right);

private:
    double sampleRate_ = 44100.0;
    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
    int writePos_ = 0;
    int loopStart_ = 0;
    int loopLen_ = 0;
    int outPos_ = 0;
    int capturedSamples_ = 0;
    int repeatCycle_ = 0;
    bool capturing_ = true;
};

// 4. Tape Stop / Vinyl Brake (Pitch deceleration effect across the combined contiguous pad span)
enum class TapeDirection : int { Stop = 0, Start };

struct TapeStopSettings {
    TapeDirection direction = TapeDirection::Stop;
    bool stepped = false;  // 8 speed steps, not a smooth ramp
    bool slip = false;
    bool lowpass = true;   // highs roll off as speed drops
    bool fade = true;      // volume drops with speed
    double fixedDurationSec = 0.0; // 0 = use the pad span
};

class TapeStopEffect {
public:
    void prepare(double sampleRate);
    void reset();
    void triggerBrake();
    void processSample(float amount, double spanDurationSec, const TapeStopSettings& settings, float& left, float& right);

private:
    double sampleRate_ = 44100.0;
    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
    int writePos_ = 0;
    double readPos_ = 0.0;
    double speed_ = 1.0;
    int elapsedSamples_ = 0;
    double slipPhase_ = 0.0;
    float tapeLpL_ = 0.0f;
    float tapeLpR_ = 0.0f;
};

// 5. Ring Modulator (Cybernetic harmonic ring mod controlled by a knob)
class RingModEffect {
public:
    void prepare(double sampleRate);
    void reset();
    void processSample(float amount, float& left, float& right);

private:
    double sampleRate_ = 44100.0;
    double phase_ = 0.0;
};

// ============================================================================
// Phase 2: 5 Additional Glitch FX (FX 6 .. FX 10)
// ============================================================================

// 6. Reverse Grain (Continuous backwards buffer player across combined contiguous pads)
class ReverseGrainEffect {
public:
    void prepare(double sampleRate);
    void reset();
    void recordDrySample(float left, float right);
    void triggerGrain();
    void processSample(float amount, float spanPhase01, double spanDurationSec, float& left, float& right);

private:
    double sampleRate_ = 44100.0;
    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
    int writePos_ = 0;
    int recordedSamples_ = 0;
    int preRollSamples_ = 0;
    int grainStartWritePos_ = 0;
    int grainPos_ = 0;
};

// 7. Chopper Gate (Rhythmic slicer / gate envelope that sustains across combined contiguous pads)
class ChopperGateEffect {
public:
    void prepare(double sampleRate);
    void reset();
    void processSample(float amount, double bpm, int spanLengthSteps, float& left, float& right);

private:
    double sampleRate_ = 44100.0;
    double phase_ = 0.0;
    float smoothGateL_ = 1.0f;
    float smoothGateR_ = 1.0f;
};

// 8. Pitch Glitch (Dual-head windowed granular pitch & octave jumper controlled by a knob)
class PitchGlitchEffect {
public:
    void prepare(double sampleRate);
    void reset();
    void recordDrySample(float left, float right);
    void processSample(float amount, float& left, float& right);

private:
    double sampleRate_ = 44100.0;
    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
    int writePos_ = 0;
    int recordedSamples_ = 0;
    double grainPhase_ = 0.0;
    double currentRatio_ = 1.0;
    int stepCounter_ = 0;
    int intervalIndex_ = 0;
};

// 9. Comb Resonator (Metallic short-delay feedback resonator controlled by a knob)
class CombResonatorEffect {
public:
    void prepare(double sampleRate);
    void reset();
    void processSample(float amount, float& left, float& right);

private:
    double sampleRate_ = 44100.0;
    std::vector<float> delayL_;
    std::vector<float> delayR_;
    int writePos_ = 0;
    double smoothDelaySamples_ = 64.0;
    float dampL_ = 0.0f;
    float dampR_ = 0.0f;
};

// 10. Wavefolder (Multi-stage harmonic digital wavefolder controlled by a knob)
class WavefolderEffect {
public:
    void prepare(double sampleRate);
    void reset();
    void processSample(float amount, float& left, float& right);

private:
    double sampleRate_ = 44100.0;
    float dcInL_ = 0.0f;
    float dcOutL_ = 0.0f;
    float dcInR_ = 0.0f;
    float dcOutR_ = 0.0f;
    float toneLpL_ = 0.0f;
    float toneLpR_ = 0.0f;
};

// Filter after the FX chain
enum class FilterMode : int { Off = 0, LowPass, HighPass };

class WetFilter {
public:
    void prepare(double sampleRate);
    void reset();
    void processSample(FilterMode mode, float cutoffHz, float resonance01, float& left, float& right);

private:
    double sampleRate_ = 44100.0;
    std::array<float, 2> ic1_{};
    std::array<float, 2> ic2_{};
};

} // namespace openglitch::core
