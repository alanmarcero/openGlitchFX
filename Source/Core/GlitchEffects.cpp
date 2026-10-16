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

    // Continuous bit depth reduction from 16 bits (amount=0) down to 1.5 bits (amount=1)
    const float bits = 16.0f - amt * 14.5f;
    const float steps = std::pow(2.0f, bits);
    const float invSteps = 1.0f / steps;

    // Subtle pre-saturation drive so low-bit quantization stays musical and punchy
    const float drive = 1.0f + amt * 1.6f;
    float inL = std::tanh(left * drive);
    float inR = std::tanh(right * drive);

    // Quantize amplitude levels (staircase bitrate reduction)
    float crushedL = std::round(inL * steps) * invSteps;
    float crushedR = std::round(inR * steps) * invSteps;

    // Bitrate packet/frame decimation at higher knob positions
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

    // Downsample factor from 1x up to 56x
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
    const size_t maxSamples = static_cast<size_t>(sampleRate_ * 1.0) + 64;
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

    // Slice duration scales from 1/8 beat down to 8ms micro-grain buzz as knob turns up
    const double maxSliceSec = std::min(0.25, quarterSec * 0.5);
    const double minSliceSec = 0.008;
    const double sliceSec = maxSliceSec * std::pow(minSliceSec / maxSliceSec, static_cast<double>(amt));
    const int sliceLen = std::clamp(static_cast<int>(sliceSec * sampleRate_), 64, maxBuf - 1);

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

    const int loopLen = std::clamp(sliceLen, 64, std::max(64, capturedSamples_));
    if (readPos_ >= loopLen) {
        readPos_ = 0;
    }

    // Raised-cosine edge window (64 samples at start/end) to avoid clicks on loop wrap
    float win = 1.0f;
    const int fadeLen = std::min(64, loopLen / 4);
    if (fadeLen > 1) {
        if (readPos_ < fadeLen) {
            win = 0.5f * (1.0f - std::cos(std::numbers::pi_v<float> * static_cast<float>(readPos_) / static_cast<float>(fadeLen)));
        } else if (readPos_ >= loopLen - fadeLen) {
            const int rem = loopLen - 1 - readPos_;
            win = 0.5f * (1.0f - std::cos(std::numbers::pi_v<float> * static_cast<float>(rem) / static_cast<float>(fadeLen)));
        }
    }

    const float stutL = bufferL_[static_cast<size_t>(readPos_)] * win;
    const float stutR = bufferR_[static_cast<size_t>(readPos_)] * win;
    ++readPos_;

    const float wet = std::min(1.0f, amt * 1.3f);
    left = left * (1.0f - wet) + stutL * wet;
    right = right * (1.0f - wet) + stutR * wet;
}

// ============================================================================
// 4. TapeStopEffect
// ============================================================================
void TapeStopEffect::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    const size_t maxSamples = static_cast<size_t>(sampleRate_ * 2.0) + 64;
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
    activeSamples_ = 0;
}

void TapeStopEffect::triggerBrake() {
    writePos_ = 0;
    readPos_ = 0.0;
    speed_ = 1.0;
    activeSamples_ = 0;
}

void TapeStopEffect::processSample(float amount, float& left, float& right) {
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

    // Deceleration time from 600ms (low knob) down to 70ms (high knob)
    const double stopSeconds = 0.60 - static_cast<double>(amt) * 0.53;
    const double decelPerSample = 1.0 / std::max(64.0, stopSeconds * sampleRate_);
    speed_ = std::max(0.0, speed_ - decelPerSample);

    const int idx0 = std::clamp(static_cast<int>(readPos_), 0, std::max(0, writePos_ - 1));
    const int idx1 = std::clamp(idx0 + 1, 0, std::max(0, writePos_ - 1));
    const float frac = static_cast<float>(readPos_ - static_cast<double>(idx0));

    const float gainEnv = static_cast<float>(std::sqrt(speed_));
    const float tapeL = (bufferL_[static_cast<size_t>(idx0)] * (1.0f - frac) + bufferL_[static_cast<size_t>(idx1)] * frac) * gainEnv;
    const float tapeR = (bufferR_[static_cast<size_t>(idx0)] * (1.0f - frac) + bufferR_[static_cast<size_t>(idx1)] * frac) * gainEnv;

    readPos_ += speed_;
    ++activeSamples_;

    // Periodic auto-retrigger if held continuously for > 1 second
    if (activeSamples_ > static_cast<int>(sampleRate_ * 0.75)) {
        triggerBrake();
    }

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

    // Exponential frequency sweep from 45 Hz to 1600 Hz
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

} // namespace openglitch::core
