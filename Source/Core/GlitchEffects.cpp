#include "GlitchEffects.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace openglitch::core {

// ============================================================================
// 1. BitrateCrusher
// ============================================================================
void BitrateCrusher::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    reset();
}

void BitrateCrusher::reset() {
    frameCounter_ = 0;
    heldL_ = 0.0f;
    heldR_ = 0.0f;
}

void BitrateCrusher::processSample(float amount, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f) {
        return;
    }

    const float bits = 16.0f - amt * 14.5f;
    const float steps = std::pow(2.0f, bits);
    const float invSteps = 1.0f / steps;

    const float drive = 1.0f + amt * 1.6f;
    float inL = std::tanh(left * drive);
    float inR = std::tanh(right * drive);

    float crushedL = std::round(inL * steps) * invSteps;
    float crushedR = std::round(inR * steps) * invSteps;

    const int packetHold = 1 + static_cast<int>(amt * amt * 12.0f);
    if (frameCounter_ <= 0) {
        heldL_ = crushedL;
        heldR_ = crushedR;
        frameCounter_ = packetHold;
    }
    --frameCounter_;

    const float wetMix = std::min(1.0f, amt * 1.25f);
    left = left * (1.0f - wetMix) + heldL_ * wetMix;
    right = right * (1.0f - wetMix) + heldR_ * wetMix;
}

// ============================================================================
// 2. SampleRateDecimator
// ============================================================================
void SampleRateDecimator::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    reset();
}

void SampleRateDecimator::reset() {
    phase_ = 1.0;
    heldL_ = 0.0f;
    heldR_ = 0.0f;
}

void SampleRateDecimator::processSample(float amount, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f) {
        return;
    }

    const double factor = 1.0 + static_cast<double>(amt * amt) * 55.0;
    const double step = 1.0 / factor;
    phase_ += step;
    if (phase_ >= 1.0) {
        phase_ -= 1.0;
        heldL_ = left;
        heldR_ = right;
    }

    left = heldL_;
    right = heldR_;
}

// ============================================================================
// 3. BufferStutter
// ============================================================================
void BufferStutter::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    const size_t maxSamples = static_cast<size_t>(sampleRate_ * 2.0) + 64;
    bufferL_.assign(maxSamples, 0.0f);
    bufferR_.assign(maxSamples, 0.0f);
    reset();
}

void BufferStutter::reset() {
    std::fill(bufferL_.begin(), bufferL_.end(), 0.0f);
    std::fill(bufferR_.begin(), bufferR_.end(), 0.0f);
    writePos_ = 0;
    readPos_ = 0;
    capturedSamples_ = 0;
    capturing_ = true;
}

void BufferStutter::triggerCapture() {
    writePos_ = 0;
    readPos_ = 0;
    capturedSamples_ = 0;
    capturing_ = true;
}

void BufferStutter::processSample(float amount, double bpm, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f || bufferL_.empty()) {
        return;
    }

    const int maxBuf = static_cast<int>(bufferL_.size());
    const double safeBpm = std::clamp(bpm, 40.0, 240.0);
    const double quarterSec = 60.0 / safeBpm;

    const double maxSliceSec = std::min(0.50, quarterSec * 1.0);
    const double minSliceSec = 0.025;
    const double sliceSec = maxSliceSec * std::pow(minSliceSec / maxSliceSec, static_cast<double>(amt));
    const int sliceLen = std::clamp(static_cast<int>(sliceSec * sampleRate_), 256, maxBuf - 1);

    if (capturing_) {
        bufferL_[static_cast<size_t>(writePos_)] = left;
        bufferR_[static_cast<size_t>(writePos_)] = right;
        ++writePos_;
        ++capturedSamples_;
        if (capturedSamples_ >= sliceLen) {
            capturing_ = false;
            readPos_ = 0;
        }
        return;
    }

    const int loopLen = std::clamp(sliceLen, 256, std::max(256, capturedSamples_));
    if (readPos_ >= loopLen) {
        readPos_ = 0;
    }

    // Equal-power boundary crossfade between loop tail and loop start so the repeat sustains seamlessly
    const int xfadeLen = std::min(128, loopLen / 4);
    float stutL = bufferL_[static_cast<size_t>(readPos_)];
    float stutR = bufferR_[static_cast<size_t>(readPos_)];
    if (xfadeLen > 1 && readPos_ >= loopLen - xfadeLen) {
        const float t = static_cast<float>(readPos_ - (loopLen - xfadeLen)) / static_cast<float>(xfadeLen);
        const int wrapIdx = readPos_ - (loopLen - xfadeLen);
        stutL = stutL * (1.0f - t) + bufferL_[static_cast<size_t>(wrapIdx)] * t;
        stutR = stutR * (1.0f - t) + bufferR_[static_cast<size_t>(wrapIdx)] * t;
    }
    ++readPos_;

    const float wet = std::min(1.0f, amt * 1.3f);
    left = left * (1.0f - wet) + stutL * wet;
    right = right * (1.0f - wet) + stutR * wet;
}

// ============================================================================
// 4. TapeStopEffect (Span-duration aware: decelerates continuously across merged pads)
// ============================================================================
void TapeStopEffect::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    const size_t maxSamples = static_cast<size_t>(sampleRate_ * 8.5) + 64;
    bufferL_.assign(maxSamples, 0.0f);
    bufferR_.assign(maxSamples, 0.0f);
    reset();
}

void TapeStopEffect::reset() {
    std::fill(bufferL_.begin(), bufferL_.end(), 0.0f);
    std::fill(bufferR_.begin(), bufferR_.end(), 0.0f);
    writePos_ = 0;
    readPos_ = 0.0;
    speed_ = 1.0;
}

void TapeStopEffect::triggerBrake() {
    writePos_ = 0;
    readPos_ = 0.0;
    speed_ = 1.0;
}

void TapeStopEffect::processSample(float amount, double spanDurationSec, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f || bufferL_.empty()) {
        return;
    }

    const int maxBuf = static_cast<int>(bufferL_.size());
    if (writePos_ < maxBuf) {
        bufferL_[static_cast<size_t>(writePos_)] = left;
        bufferR_[static_cast<size_t>(writePos_)] = right;
        ++writePos_;
    }

    // Scale deceleration across the combined contiguous pad span!
    // Keeps a low-speed sustain floor so long linked spans glide down smoothly instead of cutting out early.
    const double clampedSpanSec = std::clamp(spanDurationSec, 0.04, 8.0);
    const double minFloorSpeed = 0.18 * (1.0 - static_cast<double>(amt) * 0.85);
    const double stopSeconds = clampedSpanSec * (1.25 - static_cast<double>(amt) * 0.55);
    const double decelPerSample = (1.0 - minFloorSpeed) / std::max(64.0, stopSeconds * sampleRate_);
    speed_ = std::max(minFloorSpeed, speed_ - decelPerSample);

    const int idx0 = std::clamp(static_cast<int>(readPos_), 0, std::max(0, writePos_ - 1));
    const int idx1 = std::clamp(idx0 + 1, 0, std::max(0, writePos_ - 1));
    const float frac = static_cast<float>(readPos_ - static_cast<double>(idx0));

    const float gainEnv = static_cast<float>(std::pow(std::max(0.05, speed_), 0.35));
    const float tapeL = (bufferL_[static_cast<size_t>(idx0)] * (1.0f - frac) + bufferL_[static_cast<size_t>(idx1)] * frac) * gainEnv;
    const float tapeR = (bufferR_[static_cast<size_t>(idx0)] * (1.0f - frac) + bufferR_[static_cast<size_t>(idx1)] * frac) * gainEnv;

    readPos_ += speed_;

    const float wet = std::min(1.0f, amt * 1.25f);
    left = left * (1.0f - wet) + tapeL * wet;
    right = right * (1.0f - wet) + tapeR * wet;
}

// ============================================================================
// 5. RingModEffect
// ============================================================================
void RingModEffect::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    reset();
}

void RingModEffect::reset() {
    phase_ = 0.0;
}

void RingModEffect::processSample(float amount, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f) {
        return;
    }

    const double freq = 45.0 * std::pow(1600.0 / 45.0, static_cast<double>(amt));
    const float carrier = std::sin(static_cast<float>(phase_ * 2.0 * std::numbers::pi));
    phase_ += freq / sampleRate_;
    if (phase_ >= 1.0) {
        phase_ -= 1.0;
    }

    const float modL = left * carrier;
    const float modR = right * carrier;
    const float wet = std::min(1.0f, amt * 1.15f);

    left = left * (1.0f - wet) + modL * wet;
    right = right * (1.0f - wet) + modR * wet;
}

// ============================================================================
// 6. ReverseGrainEffect (Span-duration aware: reverses continuously across merged pads)
// ============================================================================
void ReverseGrainEffect::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    const size_t maxBuf = static_cast<size_t>(sampleRate_ * 8.5) + 64;
    bufferL_.assign(maxBuf, 0.0f);
    bufferR_.assign(maxBuf, 0.0f);
    reset();
}

void ReverseGrainEffect::reset() {
    std::fill(bufferL_.begin(), bufferL_.end(), 0.0f);
    std::fill(bufferR_.begin(), bufferR_.end(), 0.0f);
    writePos_ = 0;
    grainStartWritePos_ = 0;
    grainPos_ = 0;
}

void ReverseGrainEffect::triggerGrain() {
    grainPos_ = 0;
}

void ReverseGrainEffect::processSample(float amount, float /*spanPhase01*/, double spanDurationSec, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    const int bufSize = static_cast<int>(bufferL_.size());
    if (amt <= 1e-4f || bufSize == 0) {
        return;
    }

    bufferL_[static_cast<size_t>(writePos_)] = left;
    bufferR_[static_cast<size_t>(writePos_)] = right;

    // Reverse grain spans the full combined multi-pad region without resetting every 1/32!
    const double clampedSpanSec = std::clamp(spanDurationSec, 0.06, 8.0);
    const int spanSamples = std::clamp(static_cast<int>(clampedSpanSec * sampleRate_), 256, bufSize / 2 - 1);

    const int posInSpan = grainPos_ % spanSamples;
    int readIdx = writePos_ - (posInSpan * 2);
    while (readIdx < 0) {
        readIdx += bufSize;
    }
    readIdx %= bufSize;

    // Only apply a tiny 128-sample de-click ramp at the outer boundaries of the combined multi-pad span
    float win = 1.0f;
    const int fadeLen = std::min(128, spanSamples / 8);
    if (fadeLen > 1) {
        if (posInSpan < fadeLen) {
            win = 0.5f * (1.0f - std::cos(std::numbers::pi_v<float> * static_cast<float>(posInSpan) / static_cast<float>(fadeLen)));
        } else if (posInSpan >= spanSamples - fadeLen) {
            const int rem = spanSamples - 1 - posInSpan;
            win = 0.5f * (1.0f - std::cos(std::numbers::pi_v<float> * static_cast<float>(rem) / static_cast<float>(fadeLen)));
        }
    }

    const float revL = bufferL_[static_cast<size_t>(readIdx)] * win;
    const float revR = bufferR_[static_cast<size_t>(readIdx)] * win;
    writePos_ = (writePos_ + 1) % bufSize;
    ++grainPos_;

    const float wet = std::min(1.0f, amt * 1.25f);
    left = left * (1.0f - wet) + revL * wet;
    right = right * (1.0f - wet) + revR * wet;
}

// ============================================================================
// 7. ChopperGateEffect (Phase 2)
// ============================================================================
void ChopperGateEffect::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    reset();
}

void ChopperGateEffect::reset() {
    phase_ = 0.0;
    smoothGate_ = 1.0f;
}

void ChopperGateEffect::processSample(float amount, double bpm, int spanLengthSteps, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f) {
        return;
    }

    const double safeBpm = std::clamp(bpm, 40.0, 240.0);
    // Scale chopper cycle across the linked span length so linked pads produce a wider, sustained swell/chop
    // rather than chopping at 1/32 rate regardless of how many pads are linked!
    const double spanScale = 1.0 / std::sqrt(static_cast<double>(std::clamp(spanLengthSteps, 1, 32)));
    const double baseHz = (safeBpm / 60.0) * (1.5 + static_cast<double>(amt) * 10.5) * spanScale;
    phase_ += baseHz / sampleRate_;
    if (phase_ >= 1.0) {
        phase_ -= 1.0;
    }

    // Smooth raised-cosine tremolo/chop wave instead of hard square wave
    const float lfo = 0.5f * (1.0f + std::cos(static_cast<float>(phase_ * 2.0 * std::numbers::pi)));
    const float shaped = std::pow(lfo, 0.7f + amt * 1.5f);
    const float targetGate = (1.0f - amt) + amt * shaped;

    smoothGate_ = targetGate + 0.96f * (smoothGate_ - targetGate);
    left *= smoothGate_;
    right *= smoothGate_;
}

// ============================================================================
// 8. PitchGlitchEffect (Phase 2)
// ============================================================================
void PitchGlitchEffect::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    const size_t bufSize = 8192;
    bufferL_.assign(bufSize, 0.0f);
    bufferR_.assign(bufSize, 0.0f);
    reset();
}

void PitchGlitchEffect::reset() {
    std::fill(bufferL_.begin(), bufferL_.end(), 0.0f);
    std::fill(bufferR_.begin(), bufferR_.end(), 0.0f);
    writePos_ = 0;
    grainPhase_ = 0.0;
    stepCounter_ = 0;
    intervalIndex_ = 0;
}

void PitchGlitchEffect::processSample(float amount, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f || bufferL_.empty()) {
        return;
    }

    const int bufSize = static_cast<int>(bufferL_.size());
    bufferL_[static_cast<size_t>(writePos_)] = left;
    bufferR_[static_cast<size_t>(writePos_)] = right;

    // Sustain each pitch shift interval across linked pads (~250ms..500ms) instead of rapid 1/32 hopping
    const int hopPeriod = std::max(4096, static_cast<int>(sampleRate_ * (0.50 - 0.25 * static_cast<double>(amt))));
    if (++stepCounter_ >= hopPeriod) {
        stepCounter_ = 0;
        intervalIndex_ = (intervalIndex_ + 1) & 3;
    }

    static constexpr std::array<double, 4> kRatios = { 1.498307, 2.0, 0.5, 1.259921 };
    const double targetRatio = 1.0 + (kRatios[static_cast<size_t>(intervalIndex_)] - 1.0) * static_cast<double>(amt);

    constexpr double kGrainSamples = 2048.0;
    const double rate = (1.0 - targetRatio) / kGrainSamples;
    grainPhase_ += rate;
    grainPhase_ -= std::floor(grainPhase_);

    const double phaseB = std::fmod(grainPhase_ + 0.5, 1.0);
    const double delayA = grainPhase_ * kGrainSamples;
    const double delayB = phaseB * kGrainSamples;

    const float winA = static_cast<float>(0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * grainPhase_)));
    const float winB = static_cast<float>(0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * phaseB)));

    auto readInterp = [&](const std::vector<float>& buf, double delay) {
        double rpos = static_cast<double>(writePos_) - delay;
        while (rpos < 0.0) rpos += static_cast<double>(bufSize);
        const int i0 = static_cast<int>(rpos) % bufSize;
        const int i1 = (i0 + 1) % bufSize;
        const float f = static_cast<float>(rpos - std::floor(rpos));
        return buf[static_cast<size_t>(i0)] * (1.0f - f) + buf[static_cast<size_t>(i1)] * f;
    };

    const float pitchedL = readInterp(bufferL_, delayA) * winA + readInterp(bufferL_, delayB) * winB;
    const float pitchedR = readInterp(bufferR_, delayA) * winA + readInterp(bufferR_, delayB) * winB;

    writePos_ = (writePos_ + 1) % bufSize;

    const float wet = std::min(1.0f, amt * 1.2f);
    left = left * (1.0f - wet) + pitchedL * wet;
    right = right * (1.0f - wet) + pitchedR * wet;
}

// ============================================================================
// 9. CombResonatorEffect (Phase 2)
// ============================================================================
void CombResonatorEffect::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    const size_t maxDelay = static_cast<size_t>(sampleRate_ * 0.05) + 64;
    delayL_.assign(maxDelay, 0.0f);
    delayR_.assign(maxDelay, 0.0f);
    reset();
}

void CombResonatorEffect::reset() {
    std::fill(delayL_.begin(), delayL_.end(), 0.0f);
    std::fill(delayR_.begin(), delayR_.end(), 0.0f);
    writePos_ = 0;
}

void CombResonatorEffect::processSample(float amount, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f || delayL_.empty()) {
        return;
    }

    const int bufSize = static_cast<int>(delayL_.size());
    const double delaySec = 0.014 * std::pow(0.0014 / 0.014, static_cast<double>(amt));
    const int delaySamples = std::clamp(static_cast<int>(delaySec * sampleRate_), 8, bufSize - 2);

    int readPos = writePos_ - delaySamples;
    if (readPos < 0) {
        readPos += bufSize;
    }

    const float fb = 0.45f + amt * 0.43f;
    const float dL = delayL_[static_cast<size_t>(readPos)];
    const float dR = delayR_[static_cast<size_t>(readPos)];

    const float nextL = std::tanh(left + dL * fb);
    const float nextR = std::tanh(right + dR * fb);

    delayL_[static_cast<size_t>(writePos_)] = nextL;
    delayR_[static_cast<size_t>(writePos_)] = nextR;
    writePos_ = (writePos_ + 1) % bufSize;

    const float wet = std::min(1.0f, amt * 1.15f);
    left = left * (1.0f - wet) + nextL * wet;
    right = right * (1.0f - wet) + nextR * wet;
}

// ============================================================================
// 10. WavefolderEffect (Phase 2)
// ============================================================================
void WavefolderEffect::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    reset();
}

void WavefolderEffect::reset() {
}

void WavefolderEffect::processSample(float amount, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f) {
        return;
    }

    const float drive = 1.0f + amt * 9.5f;
    const float foldL = std::sin(left * drive);
    const float foldR = std::sin(right * drive);

    const float wet = std::min(1.0f, amt * 1.15f);
    left = left * (1.0f - wet) + foldL * wet;
    right = right * (1.0f - wet) + foldR * wet;
}

} // namespace openglitch::core
