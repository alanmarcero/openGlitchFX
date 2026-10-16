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
};

// 3. Buffer Stutter / Beat Repeat (Micro-slice loop repeater controlled by a knob)
class BufferStutter {
public:
    void prepare(double sampleRate);
    void reset();
    void triggerCapture();
    void processSample(float amount, double bpm, float& left, float& right);

private:
    double sampleRate_ = 44100.0;
    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
    int writePos_ = 0;
    int readPos_ = 0;
    int capturedSamples_ = 0;
    bool capturing_ = true;
};

// 4. Tape Stop / Vinyl Brake (Pitch deceleration effect across the combined contiguous pad span)
class TapeStopEffect {
public:
    void prepare(double sampleRate);
    void reset();
    void triggerBrake();
    void processSample(float amount, double spanDurationSec, float& left, float& right);

private:
    double sampleRate_ = 44100.0;
    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
    int writePos_ = 0;
    double readPos_ = 0.0;
    double speed_ = 1.0;
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
    void triggerGrain();
    void processSample(float amount, double spanDurationSec, float& left, float& right);

private:
    double sampleRate_ = 44100.0;
    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
    int writePos_ = 0;
    int grainPos_ = 0;
};

// 7. Chopper Gate (High-speed rhythmic slicer / tremolo chopper controlled by a knob)
class ChopperGateEffect {
public:
    void prepare(double sampleRate);
    void reset();
    void processSample(float amount, double bpm, float& left, float& right);

private:
    double sampleRate_ = 44100.0;
    double phase_ = 0.0;
    float smoothGate_ = 1.0f;
};

// 8. Pitch Glitch (Dual-head windowed granular pitch & octave jumper controlled by a knob)
class PitchGlitchEffect {
public:
    void prepare(double sampleRate);
    void reset();
    void processSample(float amount, float& left, float& right);

private:
    double sampleRate_ = 44100.0;
    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
    int writePos_ = 0;
    double grainPhase_ = 0.0;
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
};

// 10. Wavefolder (Multi-stage harmonic digital wavefolder controlled by a knob)
class WavefolderEffect {
public:
    void prepare(double sampleRate);
    void reset();
    void processSample(float amount, float& left, float& right);

private:
    double sampleRate_ = 44100.0;
};

} // namespace openglitch::core
