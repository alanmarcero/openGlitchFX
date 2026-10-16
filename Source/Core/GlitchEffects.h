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

// 4. Tape Stop / Vinyl Brake (Pitch deceleration effect controlled by a knob)
class TapeStopEffect {
public:
    void prepare(double sampleRate);
    void reset();
    void triggerBrake();
    void processSample(float amount, float& left, float& right);

private:
    double sampleRate_ = 44100.0;
    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
    int writePos_ = 0;
    double readPos_ = 0.0;
    double speed_ = 1.0;
    int activeSamples_ = 0;
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

} // namespace openglitch::core
