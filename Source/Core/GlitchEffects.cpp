#include "GlitchEffects.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace openglitch::core {

namespace {

// 4-point, 3rd-order Hermite fractional delay interpolation on a circular buffer
inline float readHermiteCircular(const std::vector<float>& buf, double readIndex) noexcept {
    const int size = static_cast<int>(buf.size());
    if (size <= 0) {
        return 0.0f;
    }
    while (readIndex < 0.0) {
        readIndex += static_cast<double>(size);
    }
    const int i0 = static_cast<int>(readIndex) % size;
    const float frac = static_cast<float>(readIndex - std::floor(readIndex));

    const int im1 = (i0 - 1 + size) % size;
    const int ip1 = (i0 + 1) % size;
    const int ip2 = (i0 + 2) % size;

    const float ym1 = buf[static_cast<size_t>(im1)];
    const float y0  = buf[static_cast<size_t>(i0)];
    const float y1  = buf[static_cast<size_t>(ip1)];
    const float y2  = buf[static_cast<size_t>(ip2)];

    const float c0 = y0;
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);

    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

} // namespace

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
    smoothL_ = 0.0f;
    smoothR_ = 0.0f;
}

void BitrateCrusher::processSample(float amount, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f) {
        return;
    }

    // Continuous bit-depth reduction from 16-bit down to 1.5-bit with mid-tread quantization
    const float bits = 16.0f - amt * 14.5f;
    // A signed signal in -1..1 spans 2^bits levels, so each unit holds half of them.
    const float steps = std::pow(2.0f, bits - 1.0f);
    const float invSteps = 1.0f / steps;

    // Asymmetric harmonic pre-drive with gain normalization
    const float drive = 1.0f + amt * 1.8f;
    const float normGain = 1.0f / std::tanh(drive * 0.85f);
    const float asymBias = 0.04f * amt;
    float inL = (std::tanh((left + asymBias) * drive) - std::tanh(asymBias * drive)) * normGain;
    float inR = (std::tanh((right - asymBias) * drive) - std::tanh(-asymBias * drive)) * normGain;

    float crushedL = std::round(inL * steps) * invSteps;
    float crushedR = std::round(inR * steps) * invSteps;

    // Codec packet-frame hold decimation
    const int packetHold = 1 + static_cast<int>(amt * amt * 12.0f);
    if (frameCounter_ <= 0) {
        heldL_ = crushedL;
        heldR_ = crushedR;
        frameCounter_ = packetHold;
    }
    --frameCounter_;

    // Subtle DAC reconstruction filter to tame harsh ultrasonic edges while keeping crunchy grit
    const float lpAlpha = 0.78f - amt * 0.18f;
    smoothL_ += lpAlpha * (heldL_ - smoothL_);
    smoothR_ += lpAlpha * (heldR_ - smoothR_);

    const float wetL = heldL_ * 0.75f + smoothL_ * 0.25f;
    const float wetR = heldR_ * 0.75f + smoothR_ * 0.25f;

    const float wetMix = std::min(1.0f, amt * 1.25f);
    left = left * (1.0f - wetMix) + wetL * wetMix;
    right = right * (1.0f - wetMix) + wetR * wetMix;
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
    prevHeldL_ = 0.0f;
    prevHeldR_ = 0.0f;
}

void SampleRateDecimator::processSample(float amount, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f) {
        return;
    }

    const double factor = 1.0 + static_cast<double>(amt * amt) * 58.0;
    const double step = 1.0 / factor;
    phase_ += step;
    if (phase_ >= 1.0) {
        phase_ -= 1.0;
        prevHeldL_ = heldL_;
        prevHeldR_ = heldR_;
        // Vintage S&H capacitor edge emphasis
        heldL_ = left + 0.08f * amt * (left - prevHeldL_);
        heldR_ = right + 0.08f * amt * (right - prevHeldR_);
    }

    const float wet = std::min(1.0f, amt * 1.35f);
    left = left * (1.0f - wet) + heldL_ * wet;
    right = right * (1.0f - wet) + heldR_ * wet;
}

// ============================================================================
// 3. BufferStutter
// ============================================================================
void BufferStutter::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    const size_t maxSamples = static_cast<size_t>(sampleRate_ * 4.0) + 64;
    bufferL_.assign(maxSamples, 0.0f);
    bufferR_.assign(maxSamples, 0.0f);
    reset();
}

void BufferStutter::reset() {
    std::fill(bufferL_.begin(), bufferL_.end(), 0.0f);
    std::fill(bufferR_.begin(), bufferR_.end(), 0.0f);
    writePos_ = 0;
    triggerCapture();
}

void BufferStutter::triggerCapture() {
    loopStart_ = writePos_;
    loopLen_ = 0;
    outPos_ = 0;
    capturedSamples_ = 0;
    repeatCycle_ = 0;
    capturing_ = true;
}

void BufferStutter::processSample(float amount, double bpm, const StutterSettings& settings, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f || bufferL_.empty()) {
        return;
    }

    const int maxBuf = static_cast<int>(bufferL_.size());
    const double safeBpm = std::clamp(bpm, 40.0, 240.0);
    const double quarterSec = 60.0 / safeBpm;

    const double maxSliceSec = std::min(0.50, quarterSec * 1.0);
    const double minSliceSec = 0.022;
    const double knobSliceSec = maxSliceSec * std::pow(minSliceSec / maxSliceSec, static_cast<double>(amt));
    const double sliceSec = settings.syncedSliceBeats > 0.0 ? settings.syncedSliceBeats * quarterSec : knobSliceSec;
    const int sliceLen = std::clamp(static_cast<int>(sliceSec * sampleRate_), 256, maxBuf / 4);

    // Advance mode reads new audio, so it keeps recording.
    if (capturing_ || settings.advance) {
        bufferL_[static_cast<size_t>(writePos_)] = left;
        bufferR_[static_cast<size_t>(writePos_)] = right;
        writePos_ = (writePos_ + 1) % maxBuf;
    }

    if (capturing_) {
        ++capturedSamples_;
        if (capturedSamples_ >= sliceLen) {
            capturing_ = false;
            loopLen_ = sliceLen;
            outPos_ = 0;
            repeatCycle_ = 1;
        }
        return;
    }

    if (outPos_ >= loopLen_) {
        outPos_ = 0;
        ++repeatCycle_;
    }

    int advanceOffset = settings.advance ? (repeatCycle_ - 1) * (loopLen_ / 2) : 0;
    if (advanceOffset + 2 * loopLen_ >= maxBuf) {
        // At the buffer limit, restart the slice on the newest audio.
        loopStart_ = (writePos_ - loopLen_ + maxBuf) % maxBuf;
        repeatCycle_ = 1;
        advanceOffset = 0;
    }

    const double pitchRatio = std::pow(2.0, static_cast<double>(settings.pitchStepSemitones) * repeatCycle_ / 12.0);
    const double rate = std::clamp(pitchRatio * settings.rateMultiplier, 0.25, 2.0);
    const bool reversed = settings.reverse == StutterReverse::All
                       || (settings.reverse == StutterReverse::Alternate && (repeatCycle_ % 2) == 0);
    const double sliceOffset = std::fmod(static_cast<double>(outPos_) * rate, static_cast<double>(loopLen_));
    const double readOffset = reversed ? static_cast<double>(loopLen_ - 1) - sliceOffset : sliceOffset;
    const double readIndex = static_cast<double>(loopStart_ + advanceOffset) + readOffset;
    float stutL = readHermiteCircular(bufferL_, readIndex);
    float stutR = readHermiteCircular(bufferR_, readIndex);

    const int gateLen = std::max(32, static_cast<int>(settings.gate * static_cast<float>(loopLen_)));
    const int fadeLen = std::min(160, gateLen / 4);
    const float fadeIn = std::min(1.0f, static_cast<float>(outPos_ + 1) / static_cast<float>(std::max(1, fadeLen / 2)));
    const float fadeOut = std::clamp(static_cast<float>(gateLen - outPos_) / static_cast<float>(std::max(1, fadeLen)), 0.0f, 1.0f);
    const float window = 0.5f - 0.5f * std::cos(std::numbers::pi_v<float> * std::min(fadeIn, fadeOut));
    stutL *= window;
    stutR *= window;

    // Subtle rhythmic micro-contour and stereo ping-pong shimmer at high stutter amounts
    if (amt > 0.55f && (repeatCycle_ & 1) != 0) {
        const float panShimmer = (amt - 0.55f) * 0.35f;
        stutL *= (1.0f - panShimmer);
        stutR *= (1.0f + panShimmer * 0.5f);
    }

    ++outPos_;

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
    elapsedSamples_ = 0;
    slipPhase_ = 0.0;
    tapeLpL_ = 0.0f;
    tapeLpR_ = 0.0f;
}

void TapeStopEffect::triggerBrake() {
    writePos_ = 0;
    readPos_ = 0.0;
    speed_ = 1.0;
    elapsedSamples_ = 0;
}

void TapeStopEffect::processSample(float amount, double spanDurationSec, const TapeStopSettings& settings, float& left, float& right) {
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

    const bool fixedLength = settings.fixedDurationSec > 0.0;
    const double clampedSpanSec = std::clamp(fixedLength ? settings.fixedDurationSec : spanDurationSec, 0.04, 8.0);
    const double minFloorSpeed = 0.14 * (1.0 - static_cast<double>(amt) * 0.86);
    const double rampScale = fixedLength ? 1.0 : (1.20 - static_cast<double>(amt) * 0.52);
    const double rampSamples = std::max(64.0, clampedSpanSec * rampScale * sampleRate_);
    double progress = std::min(1.0, static_cast<double>(elapsedSamples_) / rampSamples);
    if (settings.stepped) {
        progress = std::floor(progress * 8.0) / 8.0;
    }
    ++elapsedSamples_;

    const double rampDown = 1.0 - (1.0 - minFloorSpeed) * progress;
    const double rampUp = minFloorSpeed + (1.0 - minFloorSpeed) * progress;
    speed_ = (settings.direction == TapeDirection::Start) ? rampUp : rampDown;

    if (settings.slip) {
        slipPhase_ += 1.0 / sampleRate_;
        const double wow = 0.045 * std::sin(2.0 * std::numbers::pi * 0.9 * slipPhase_);
        const double flutter = 0.012 * std::sin(2.0 * std::numbers::pi * 7.3 * slipPhase_);
        speed_ = std::max(0.02, speed_ * (1.0 + static_cast<double>(amt) * (wow + flutter)));
    }

    // 4-point cubic Hermite interpolation within the captured buffer
    const int maxValid = std::max(1, writePos_ - 1);
    const double clampedRead = std::clamp(readPos_, 0.0, static_cast<double>(maxValid));
    const int i0 = std::clamp(static_cast<int>(clampedRead), 0, maxValid);
    const int im1 = std::max(0, i0 - 1);
    const int ip1 = std::min(maxValid, i0 + 1);
    const int ip2 = std::min(maxValid, i0 + 2);
    const float frac = static_cast<float>(clampedRead - static_cast<double>(i0));

    auto hermite = [frac](float ym1, float y0, float y1, float y2) noexcept {
        const float c0 = y0;
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    };

    const float rawL = hermite(bufferL_[static_cast<size_t>(im1)],
                               bufferL_[static_cast<size_t>(i0)],
                               bufferL_[static_cast<size_t>(ip1)],
                               bufferL_[static_cast<size_t>(ip2)]);
    const float rawR = hermite(bufferR_[static_cast<size_t>(im1)],
                               bufferR_[static_cast<size_t>(i0)],
                               bufferR_[static_cast<size_t>(ip1)],
                               bufferR_[static_cast<size_t>(ip2)]);

    // Speed-coupled analog tape-head high-frequency rolloff as reel decelerates
    const float headAlpha = settings.lowpass ? static_cast<float>(std::clamp(0.15 + 0.85 * speed_, 0.15, 1.0)) : 1.0f;
    tapeLpL_ += headAlpha * (rawL - tapeLpL_);
    tapeLpR_ += headAlpha * (rawR - tapeLpR_);

    const float gainEnv = settings.fade ? static_cast<float>(std::pow(std::max(0.04, speed_), 0.36)) : 1.0f;
    const float tapeL = tapeLpL_ * gainEnv;
    const float tapeR = tapeLpR_ * gainEnv;

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
    const float twoPiPhase = static_cast<float>(phase_ * 2.0 * std::numbers::pi);
    // Stereo quadrature carrier spread + subtle 3rd-harmonic cyber overtone
    const float stereoOffset = 0.42f * amt;
    const float harmBlend = 0.15f * amt;
    const float carrierL = std::sin(twoPiPhase) + harmBlend * std::sin(3.0f * twoPiPhase);
    const float carrierR = std::sin(twoPiPhase + stereoOffset) + harmBlend * std::sin(3.0f * (twoPiPhase + stereoOffset));

    phase_ += freq / sampleRate_;
    if (phase_ >= 1.0) {
        phase_ -= 1.0;
    }

    const float modL = left * carrierL;
    const float modR = right * carrierR;
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
    recordedSamples_ = 0;
    preRollSamples_ = 0;
    grainStartWritePos_ = 0;
    grainPos_ = 0;
}

void ReverseGrainEffect::recordDrySample(float left, float right) {
    const int bufSize = static_cast<int>(bufferL_.size());
    if (bufSize <= 0) {
        return;
    }
    bufferL_[static_cast<size_t>(writePos_)] = left;
    bufferR_[static_cast<size_t>(writePos_)] = right;
    writePos_ = (writePos_ + 1) % bufSize;
    if (recordedSamples_ < bufSize) {
        ++recordedSamples_;
    }
}

void ReverseGrainEffect::triggerGrain() {
    grainStartWritePos_ = writePos_;
    preRollSamples_ = recordedSamples_;
    grainPos_ = 0;
}

void ReverseGrainEffect::processSample(float amount, float /*spanPhase01*/, double spanDurationSec, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    const int bufSize = static_cast<int>(bufferL_.size());
    if (amt <= 1e-4f || bufSize == 0) {
        return;
    }

    // Reverse grain spans the full combined multi-pad region without resetting every 1/32!
    const double clampedSpanSec = std::clamp(spanDurationSec, 0.06, 8.0);
    const int spanSamples = std::clamp(static_cast<int>(clampedSpanSec * sampleRate_), 256, bufSize / 2 - 1);
    const int posInSpan = grainPos_ % spanSamples;

    int readIdx = 0;
    if (preRollSamples_ >= 256) {
        // Pre-warmed buffer: step backwards continuously from grainStartWritePos_ through pre-roll history
        const int revLen = std::min(spanSamples, preRollSamples_);
        const int backFromStart = posInSpan % revLen;
        readIdx = grainStartWritePos_ - 1 - backFromStart;
    } else if (grainPos_ >= 256) {
        // Cold start (playback started directly on an active pad): reverse each completed 256-sample grain
        const int chunkIdx = grainPos_ / 256;
        const int offsetInChunk = grainPos_ % 256;
        readIdx = grainStartWritePos_ + (chunkIdx * 256 - 1 - offsetInChunk);
    } else {
        // First 256 samples of a cold start: half-speed pre-fill
        readIdx = grainStartWritePos_ + (grainPos_ / 2);
    }
    while (readIdx < 0) {
        readIdx += bufSize;
    }
    readIdx %= bufSize;

    // Apply a smooth 128-sample raised-cosine de-click ramp at the outer boundaries of the combined span
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
    smoothGateL_ = 1.0f;
    smoothGateR_ = 1.0f;
}

void ChopperGateEffect::processSample(float amount, double bpm, int spanLengthSteps, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f) {
        return;
    }

    const double safeBpm = std::clamp(bpm, 40.0, 240.0);
    // Scale chopper cycle across the linked span length so linked pads produce a wider, sustained swell/chop
    const double spanScale = 1.0 / std::sqrt(static_cast<double>(std::clamp(spanLengthSteps, 1, 32)));
    const double baseHz = (safeBpm / 60.0) * (1.5 + static_cast<double>(amt) * 10.5) * spanScale;
    phase_ += baseHz / sampleRate_;
    if (phase_ >= 1.0) {
        phase_ -= 1.0;
    }

    // Smooth raised-cosine trance-gate wave with stereo phase offset at high knob settings
    const float twoPi = 2.0f * std::numbers::pi_v<float>;
    const float stereoSpread = std::max(0.0f, amt - 0.65f) * 0.6f;
    const float lfoL = 0.5f * (1.0f + std::cos(static_cast<float>(phase_) * twoPi));
    const float lfoR = 0.5f * (1.0f + std::cos((static_cast<float>(phase_) + stereoSpread) * twoPi));

    const float exponent = 0.7f + amt * 1.65f;
    const float shapedL = std::pow(lfoL, exponent);
    const float shapedR = std::pow(lfoR, exponent);

    const float targetGateL = (1.0f - amt) + amt * shapedL;
    const float targetGateR = (1.0f - amt) + amt * shapedR;

    smoothGateL_ = targetGateL + 0.95f * (smoothGateL_ - targetGateL);
    smoothGateR_ = targetGateR + 0.95f * (smoothGateR_ - targetGateR);
    left *= smoothGateL_;
    right *= smoothGateR_;
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
    recordedSamples_ = 0;
    grainPhase_ = 0.0;
    currentRatio_ = 1.0;
    stepCounter_ = 0;
    intervalIndex_ = 0;
}

void PitchGlitchEffect::recordDrySample(float left, float right) {
    const int bufSize = static_cast<int>(bufferL_.size());
    if (bufSize <= 0) {
        return;
    }
    bufferL_[static_cast<size_t>(writePos_)] = left;
    bufferR_[static_cast<size_t>(writePos_)] = right;
    writePos_ = (writePos_ + 1) % bufSize;
    if (recordedSamples_ < bufSize) {
        ++recordedSamples_;
    }
}

void PitchGlitchEffect::processSample(float amount, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f || bufferL_.empty()) {
        return;
    }

    const int bufSize = static_cast<int>(bufferL_.size());
    const int latestWrite = (writePos_ - 1 + bufSize) % bufSize;

    // Sustain each pitch shift interval across linked pads (~250ms..500ms) instead of rapid 1/32 hopping
    const int hopPeriod = std::max(4096, static_cast<int>(sampleRate_ * (0.50 - 0.25 * static_cast<double>(amt))));
    if (++stepCounter_ >= hopPeriod) {
        stepCounter_ = 0;
        intervalIndex_ = (intervalIndex_ + 1) & 3;
    }

    // +7, +12, -12 and +19 semitones
    static constexpr std::array<double, 4> kRatios = { 1.498307, 2.0, 0.5, 2.996614 };
    const double targetRatio = 1.0 + (kRatios[static_cast<size_t>(intervalIndex_)] - 1.0) * static_cast<double>(amt);
    currentRatio_ += 0.015 * (targetRatio - currentRatio_);

    constexpr double kGrainSamples = 2048.0;
    const double maxAvailableDelay = static_cast<double>(std::max(16, std::min(recordedSamples_ - 4, 2048)));
    const double effectiveGrain = std::min(kGrainSamples, maxAvailableDelay);

    const double rate = (1.0 - currentRatio_) / effectiveGrain;
    grainPhase_ += rate;
    grainPhase_ -= std::floor(grainPhase_);

    const double phaseB = std::fmod(grainPhase_ + 0.5, 1.0);
    const double delayA = grainPhase_ * effectiveGrain;
    const double delayB = phaseB * effectiveGrain;

    const float winA = static_cast<float>(0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * grainPhase_)));
    const float winB = static_cast<float>(0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * phaseB)));

    const double rposA = static_cast<double>(latestWrite) - delayA;
    const double rposB = static_cast<double>(latestWrite) - delayB;

    const float pitchedL = readHermiteCircular(bufferL_, rposA) * winA + readHermiteCircular(bufferL_, rposB) * winB;
    const float pitchedR = readHermiteCircular(bufferR_, rposA) * winA + readHermiteCircular(bufferR_, rposB) * winB;

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
    smoothDelaySamples_ = 0.008 * sampleRate_;
    dampL_ = 0.0f;
    dampR_ = 0.0f;
}

void CombResonatorEffect::processSample(float amount, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f || delayL_.empty()) {
        return;
    }

    const int bufSize = static_cast<int>(delayL_.size());
    const double delaySec = 0.014 * std::pow(0.0014 / 0.014, static_cast<double>(amt));
    const double targetDelay = std::clamp(delaySec * sampleRate_, 6.0, static_cast<double>(bufSize - 4));
    smoothDelaySamples_ += 0.008 * (targetDelay - smoothDelaySamples_);

    // Fractional Hermite delay with subtle stereo detune between L and R
    const double readPosL = static_cast<double>(writePos_) - smoothDelaySamples_;
    const double readPosR = static_cast<double>(writePos_) - smoothDelaySamples_ * (1.0 + 0.018 * static_cast<double>(amt));

    const float dL = readHermiteCircular(delayL_, readPosL);
    const float dR = readHermiteCircular(delayR_, readPosR);

    // Karplus-Strong style one-pole feedback damping filter so metallic resonance stays musical
    const float dampCoeff = 0.72f + 0.18f * amt;
    dampL_ += dampCoeff * (dL - dampL_);
    dampR_ += dampCoeff * (dR - dampR_);

    const float fb = 0.48f + amt * 0.42f;
    const float nextL = std::tanh(left + dampL_ * fb);
    const float nextR = std::tanh(right + dampR_ * fb);

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
    dcInL_ = 0.0f;
    dcOutL_ = 0.0f;
    dcInR_ = 0.0f;
    dcOutR_ = 0.0f;
    toneLpL_ = 0.0f;
    toneLpR_ = 0.0f;
    previousL_ = 0.0;
    previousR_ = 0.0;
}

void WavefolderEffect::processSample(float amount, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f) {
        return;
    }

    // Multi-stage West-Coast (Buchla-inspired) wavefolder: sin(u + d sin 2u), with u = drive * x.
    // To first order in d (at most 0.32), that is (1 + d/2) sin u + (d/2) sin 3u, which has a closed-form
    // antiderivative. First-order antiderivative anti-aliasing (Parker et al., 2016) then outputs the mean
    // of the fold between the previous and current input, which cuts aliasing by about 10 dB.
    const double drive = 1.0 + static_cast<double>(amt) * 9.0;
    const double depth = 0.32 * static_cast<double>(amt);
    const auto fold = [&](float input, double& previous) {
        const double x = input;
        const double delta = x - previous;
        double folded = 0.0;
        if (std::abs(delta) < 1.0e-6) {
            const double u = drive * 0.5 * (x + previous);
            folded = (1.0 + depth / 2.0) * std::sin(u) + depth / 2.0 * std::sin(3.0 * u);
        } else {
            const auto antiderivative = [&](double at) {
                const double u = drive * at;
                return -((1.0 + depth / 2.0) * std::cos(u) + depth / 6.0 * std::cos(3.0 * u)) / drive;
            };
            folded = (antiderivative(x) - antiderivative(previous)) / delta;
        }
        previous = x;
        return static_cast<float>(folded);
    };
    const float foldL = fold(left, previousL_);
    const float foldR = fold(right, previousR_);

    // DC blocker (R = 0.995)
    constexpr float kDcR = 0.995f;
    dcOutL_ = foldL - dcInL_ + kDcR * dcOutL_;
    dcInL_ = foldL;
    dcOutR_ = foldR - dcInR_ + kDcR * dcOutR_;
    dcInR_ = foldR;

    // One-pole tone contour
    const float toneAlpha = 0.82f - 0.18f * amt;
    toneLpL_ += toneAlpha * (dcOutL_ - toneLpL_);
    toneLpR_ += toneAlpha * (dcOutR_ - toneLpR_);

    const float wet = std::min(1.0f, amt * 1.15f);
    left = left * (1.0f - wet) + toneLpL_ * wet;
    right = right * (1.0f - wet) + toneLpR_ * wet;
}

// ============================================================================
// WetFilter (TPT state-variable filter)
// ============================================================================
void WetFilter::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    reset();
}

void WetFilter::reset() {
    ic1_.fill(0.0f);
    ic2_.fill(0.0f);
}

void WetFilter::processSample(FilterMode mode, float cutoffHz, float resonance01, float& left, float& right) {
    if (mode == FilterMode::Off) {
        return;
    }

    const double nyquistSafe = sampleRate_ * 0.45;
    const double fc = std::clamp(static_cast<double>(cutoffHz), 20.0, nyquistSafe);
    const auto g = static_cast<float>(std::tan(std::numbers::pi * fc / sampleRate_));
    const float k = 2.0f - 1.9f * std::clamp(resonance01, 0.0f, 1.0f);
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;

    auto tick = [&](size_t ch, float v0) noexcept {
        const float v3 = v0 - ic2_[ch];
        const float v1 = a1 * ic1_[ch] + a2 * v3;
        const float v2 = ic2_[ch] + a2 * ic1_[ch] + a3 * v3;
        ic1_[ch] = 2.0f * v1 - ic1_[ch];
        ic2_[ch] = 2.0f * v2 - ic2_[ch];
        return (mode == FilterMode::LowPass) ? v2 : (v0 - k * v1 - v2);
    };

    left = tick(0, left);
    right = tick(1, right);
}

} // namespace openglitch::core
