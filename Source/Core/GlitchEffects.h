#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace openglitch::core {

// 1. Bitrate Glitch Effect (2x oversampled bit-depth & packet bitrate quantization)
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
    float prevHeldL_ = 0.0f;
    float prevHeldR_ = 0.0f;
    float prevInL_ = 0.0f;
    float prevInR_ = 0.0f;
    std::array<float, 2> preLp1_{};
    std::array<float, 2> postLp1_{};
    std::array<float, 2> postLp2_{};
};

// 2. Sample Rate Decimator (Anti-aliased PolyBLEP sample-and-hold clock divider)
class SampleRateDecimator {
public:
    void prepare(double sampleRate);
    void reset();
    void processSample(float amount, float& left, float& right);

private:
    double sampleRate_ = 44100.0;
    double phase_ = 1.0;
    float heldL_ = 0.0f;
    float heldR_ = 0.0f;
    float prevHeldL_ = 0.0f;
    float prevHeldR_ = 0.0f;
    float pendingOutL_ = 0.0f;
    float pendingOutR_ = 0.0f;
    float prevDryL_ = 0.0f;
    float prevDryR_ = 0.0f;
    bool primed_ = false;
    std::array<float, 2> aaIc1_{};
    std::array<float, 2> aaIc2_{};
};

// 3. Buffer Stutter / Beat Repeat (Dual-head overlap-add crossfading loop repeater)
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
    struct SpliceHead {
        bool active = false;
        double readIndex = 0.0;
        double stepDelta = 1.0;
        int xfadePos = 0;
        int xfadeLen = 1;
    };

    double sampleRate_ = 44100.0;
    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
    int writePos_ = 0;
    int loopStart_ = 0;
    int loopLen_ = 0;
    int outPos_ = 0;
    int capturedSamples_ = 0;
    int repeatCycle_ = 0;
    int postCaptureGuard_ = 0;
    bool capturing_ = true;
    bool hasValidSlice_ = false;
    double lastReadIndex_ = 0.0;
    double lastStepDelta_ = 1.0;
    float smoothPanShimmer_ = 0.0f;
    SpliceHead outgoing_{};
};

// 4. Tape Stop / Vinyl Brake (Circular varispeed ring buffer + dynamic polyphase sinc resampler)
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
    void recordDrySample(float left, float right);
    void triggerBrake();
    void processSample(float amount, double spanDurationSec, const TapeStopSettings& settings, float& left, float& right);

private:
    double sampleRate_ = 44100.0;
    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
    int writePos_ = 0;
    int recordedSamples_ = 0;
    double lagSamples_ = 0.0;
    double speed_ = 1.0;
    int elapsedSamples_ = 0;
    double rampSamples_ = 0.0;
    double slipPhase_ = 0.0;
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

// 6. Reverse Grain (Hermite-interpolated overlapping raised-cosine dual-grain backwards player)
class ReverseGrainEffect {
public:
    void prepare(double sampleRate);
    void reset();
    void recordDrySample(float left, float right);
    void triggerGrain();
    void processSample(float amount, float spanPhase01, double spanDurationSec, float& left, float& right);

private:
    double alignZeroCrossing(int anchorWritePos) const noexcept;

    double sampleRate_ = 44100.0;
    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
    int writePos_ = 0;
    int recordedSamples_ = 0;
    int preRollSamples_ = 0;
    double grainStartPos_ = 0.0;
    double grainPos_ = 0.0;
    double spanSamples_ = 0.0;
    double lastReadPos_ = 0.0;
    double outgoingReadPos_ = 0.0;
    float previousSpanPhase_ = 0.0f;
    bool grainActive_ = false;
    bool outgoingActive_ = false;
    bool fadeFromDry_ = true;
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

// 8. Pitch Glitch (WSOLA pitch-synchronous dual-grain pitch & octave jumper)
class PitchGlitchEffect {
public:
    void prepare(double sampleRate);
    void reset();
    void recordDrySample(float left, float right);
    void processSample(float amount, float& left, float& right);

private:
    double findWsolaSpliceStart(int latestWrite, double nominalStart, double refPos, double grainLen) const noexcept;

    double sampleRate_ = 44100.0;
    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
    int writePos_ = 0;
    int recordedSamples_ = 0;
    double grainPhase_ = 0.0;
    double grainLenSamples_ = 768.0;
    std::array<double, 2> readPos_{};
    std::array<bool, 2> headInitialized_{};
    double currentRatio_ = 1.0;
    float fastEnv_ = 0.0f;
    float slowEnv_ = 0.0f;
    int stepCounter_ = 0;
    int intervalIndex_ = 0;
};

// 9. Comb Resonator (Metallic short-delay feedback resonator with send-gate support)
class CombResonatorEffect {
public:
    void prepare(double sampleRate);
    void reset();
    void processSample(float amount, float& left, float& right);
    void processSendSample(float amount, float sendGate, float& left, float& right);
    bool hasAudibleTail() const noexcept { return tailEnergy_ > 1e-10f; }

private:
    double sampleRate_ = 44100.0;
    std::vector<float> delayL_;
    std::vector<float> delayR_;
    int writePos_ = 0;
    double smoothDelaySamples_ = 64.0;
    float dampL_ = 0.0f;
    float dampR_ = 0.0f;
    float tailEnergy_ = 0.0f;
};

// 10. Wavefolder (Multi-stage harmonic digital wavefolder controlled by a knob)
class WavefolderEffect {
public:
    void prepare(double sampleRate);
    void reset();
    void processSample(float amount, float& left, float& right);

private:
    double sampleRate_ = 44100.0;
    float dcPole_ = 0.995f;
    float dcInL_ = 0.0f;
    float dcOutL_ = 0.0f;
    float dcInR_ = 0.0f;
    float dcOutR_ = 0.0f;
    float toneLpL_ = 0.0f;
    float toneLpR_ = 0.0f;
    double previousL_ = 0.0;
    double previousR_ = 0.0;
};

class ModulatedDelayEffect {
public:
    enum class Mode { Chorus, Flanger };
    void prepare(double sampleRate);
    void reset();
    void processSample(Mode mode, float amount, float& left, float& right);
private:
    double sampleRate_ = 44100.0;
    double phase_ = 0.0;
    int writePos_ = 0;
    std::array<std::vector<float>, 2> delay_;
    std::array<std::array<float, 2>, 2> thiranState_{};
    std::array<std::array<float, 2>, 2> thiranTap2State_{};
    std::array<std::array<float, 2>, 2> dispState_{};
    std::array<float, 2> fbLp1_{};
    std::array<float, 2> fbLp2_{};
    std::array<float, 2> fbHpIn_{};
    std::array<float, 2> fbHpOut_{};
};

class PhaserEffect {
public:
    static constexpr size_t kNumStages = 12;
    void prepare(double sampleRate);
    void reset();
    void processSample(float amount, float& left, float& right);
private:
    double sampleRate_ = 44100.0;
    double phase_ = 0.0;
    std::array<std::array<float, kNumStages>, 2> state_{};
    std::array<float, 2> feedback_{};
    std::array<float, 2> fbHpState_{};
    std::array<float, 2> fbLpState_{};
};

class PingPongEchoEffect {
public:
    void prepare(double sampleRate);
    void reset();
    void processSample(float amount, double bpm, float& left, float& right);
    void processSendSample(float amount, double bpm, float sendGate, float& left, float& right);
private:
    double sampleRate_ = 44100.0;
    double activeDelaySamples_ = 0.0;
    double prevDelaySamples_ = 0.0;
    double targetDelaySamples_ = 0.0;
    float morphPhase_ = 1.0f;
    float morphStep_ = 0.001f;
    int writePos_ = 0;
    std::array<std::vector<float>, 2> delay_;
    std::array<std::array<float, 2>, 2> thiranAct_{};
    std::array<std::array<float, 2>, 2> thiranPrev_{};
    std::array<float, 2> hpIc1_{};
    std::array<float, 2> hpIc2_{};
    std::array<float, 2> lpIc1_{};
    std::array<float, 2> lpIc2_{};
};

class SaturationEffect {
public:
    void prepare(double sampleRate);
    void reset();
    void processSample(float amount, float& left, float& right);
private:
    std::array<double, 2> previous_{};
    std::array<float, 2> dcIn_{};
    std::array<float, 2> dcOut_{};
    float dcPole_ = 0.995f;
};

// Per-sample smoothed ZDF SVF filter after the FX chain
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
    float smoothLogCutoff_ = 0.0f;
    float smoothResonance_ = 0.2f;
    float coeffSmoothAlpha_ = 0.02f;
    bool initialized_ = false;
};

} // namespace openglitch::core
