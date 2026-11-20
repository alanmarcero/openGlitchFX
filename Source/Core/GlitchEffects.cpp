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
    double wrapped = std::fmod(readIndex, static_cast<double>(size));
    if (wrapped < 0.0) {
        wrapped += static_cast<double>(size);
    }
    const int i0 = static_cast<int>(wrapped) % size;
    const float frac = static_cast<float>(wrapped - std::floor(wrapped));

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

// Padé [5/4] rational polynomial approximation of tan(w) for w in [0, 0.46 * pi]
inline float fastTan(float w) noexcept {
    const float clamped = std::clamp(w, 0.0f, 1.445f);
    const float w2 = clamped * clamped;
    const float num = clamped * (945.0f + w2 * (-105.0f + w2));
    const float den = 945.0f + w2 * (-420.0f + 15.0f * w2);
    return num / den;
}

// Click-free dual-staggered 1st-order Thiran allpass fractional delay reader
inline float readThiranDualHead(const std::vector<float>& buf,
                                int writePos,
                                double delaySamples,
                                std::array<float, 2>& state) noexcept {
    const int size = static_cast<int>(buf.size());
    if (size <= 4) {
        return 0.0f;
    }
    const double d = std::clamp(delaySamples, 2.0, static_cast<double>(size - 3));
    const double fracD = d - std::floor(d);

    // Head A switches integer tap only at fracD == 0.5, where wA == 0.
    const int intA = static_cast<int>(std::floor(d - 0.5));
    const float deltaA = static_cast<float>(d - static_cast<double>(intA));
    const float a1A = (1.0f - deltaA) / (1.0f + deltaA);
    const float xA0 = buf[static_cast<size_t>((writePos - intA + size) % size)];
    const float xA1 = buf[static_cast<size_t>((writePos - intA - 1 + size) % size)];
    const float yA = a1A * (xA0 - state[0]) + xA1;
    state[0] = yA;

    // Head B switches integer tap only at fracD == 0.0, where wB == 0.
    const int intB = static_cast<int>(std::floor(d)) - 1;
    const float deltaB = static_cast<float>(d - static_cast<double>(intB));
    const float a1B = (1.0f - deltaB) / (1.0f + deltaB);
    const float xB0 = buf[static_cast<size_t>((writePos - intB + size) % size)];
    const float xB1 = buf[static_cast<size_t>((writePos - intB - 1 + size) % size)];
    const float yB = a1B * (xB0 - state[1]) + xB1;
    state[1] = yB;

    const float wA = 0.5f * (1.0f + std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(fracD)));
    return yA * wA + yB * (1.0f - wA);
}

struct PolyphaseSincTable {
    static constexpr int kCutoffs = 16;
    static constexpr int kPhases = 64;
    static constexpr int kTaps = 16;
    static constexpr int kCenter = 7;
    std::array<std::array<std::array<float, kTaps>, kPhases + 1>, kCutoffs> weights{};

    PolyphaseSincTable() {
        for (int c = 0; c < kCutoffs; ++c) {
            const double t = static_cast<double>(c) / static_cast<double>(kCutoffs - 1);
            const double fc = 0.022 * std::pow(0.48 / 0.022, t);
            for (int p = 0; p <= kPhases; ++p) {
                const double frac = static_cast<double>(p) / static_cast<double>(kPhases);
                double sum = 0.0;
                for (int k = 0; k < kTaps; ++k) {
                    const double x = static_cast<double>(k - kCenter) - frac;
                    const double winPos = std::clamp((x + static_cast<double>(kTaps) * 0.5) / static_cast<double>(kTaps), 0.0, 1.0);
                    const double win = 0.42 - 0.5 * std::cos(2.0 * std::numbers::pi * winPos)
                                     + 0.08 * std::cos(4.0 * std::numbers::pi * winPos);
                    const double arg = 2.0 * std::numbers::pi * fc * x;
                    double sinc = 1.0;
                    if (std::abs(arg) >= 1e-8) sinc = std::sin(arg) / arg;
                    const double w = sinc * win;
                    weights[static_cast<size_t>(c)][static_cast<size_t>(p)][static_cast<size_t>(k)] = static_cast<float>(w);
                    sum += w;
                }
                const float invSum = (std::abs(sum) > 1e-12) ? static_cast<float>(1.0 / sum) : 0.0f;
                for (int k = 0; k < kTaps; ++k) {
                    weights[static_cast<size_t>(c)][static_cast<size_t>(p)][static_cast<size_t>(k)] *= invSum;
                }
            }
        }
    }
};

const PolyphaseSincTable& sincTable() {
    static const PolyphaseSincTable kTable{};
    return kTable;
}

} // namespace

// ============================================================================
// 1. BitrateCrusher (2x oversampled anti-aliased pre-LPF + PolyBLEP packet hold)
// ============================================================================
void BitrateCrusher::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    reset();
}

void BitrateCrusher::reset() {
    frameCounter_ = 0;
    heldL_ = 0.0f;
    heldR_ = 0.0f;
    prevHeldL_ = 0.0f;
    prevHeldR_ = 0.0f;
    prevInL_ = 0.0f;
    prevInR_ = 0.0f;
    preLp1_.fill(0.0f);
    postLp1_.fill(0.0f);
    postLp2_.fill(0.0f);
}

void BitrateCrusher::processSample(float amount, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f) {
        prevInL_ = left;
        prevInR_ = right;
        return;
    }

    const float dryL = left;
    const float dryR = right;

    // Continuous bit-depth reduction from 16-bit down to 1.5-bit with mid-tread quantization
    const float bits = 16.0f - amt * 14.5f;
    const float steps = std::pow(2.0f, bits - 1.0f);
    const float invSteps = 1.0f / steps;

    // Asymmetric harmonic pre-drive with exact DC/full-scale normalization
    const float drive = 1.0f + amt * 1.8f;
    const float normGain = 1.0f / std::tanh(drive * 0.85f);
    const float asymBias = 0.04f * amt;
    const float biasOffsetL = std::tanh(asymBias * drive);
    const float biasOffsetR = std::tanh(-asymBias * drive);

    const int packetHold2x = 2 * (1 + static_cast<int>(amt * amt * 12.0f));
    const float preAlpha = 0.62f - 0.14f * amt;
    const float postAlpha = 0.68f - 0.12f * amt;

    // 2x oversampled anti-aliasing pre-filter, quantizer, BLEP packet hold, and decimation LPF
    const std::array<float, 2> subL = { 0.5f * (prevInL_ + dryL), dryL };
    const std::array<float, 2> subR = { 0.5f * (prevInR_ + dryR), dryR };
    prevInL_ = dryL;
    prevInR_ = dryR;

    float decimatedL = 0.0f;
    float decimatedR = 0.0f;
    for (size_t s = 0; s < 2; ++s) {
        preLp1_[0] += preAlpha * (subL[s] - preLp1_[0]);
        preLp1_[1] += preAlpha * (subR[s] - preLp1_[1]);

        const float inL = (std::tanh((preLp1_[0] + asymBias) * drive) - biasOffsetL) * normGain;
        const float inR = (std::tanh((preLp1_[1] - asymBias) * drive) - biasOffsetR) * normGain;
        const float crushedL = std::round(inL * steps) * invSteps;
        const float crushedR = std::round(inR * steps) * invSteps;

        float stepL = heldL_;
        float stepR = heldR_;
        if (frameCounter_ <= 0) {
            prevHeldL_ = heldL_;
            prevHeldR_ = heldR_;
            heldL_ = crushedL;
            heldR_ = crushedR;
            frameCounter_ = packetHold2x;
            stepL = 0.5f * (prevHeldL_ + heldL_);
            stepR = 0.5f * (prevHeldR_ + heldR_);
        }
        --frameCounter_;

        postLp1_[0] += postAlpha * (stepL - postLp1_[0]);
        postLp1_[1] += postAlpha * (stepR - postLp1_[1]);
        postLp2_[0] += postAlpha * (postLp1_[0] - postLp2_[0]);
        postLp2_[1] += postAlpha * (postLp1_[1] - postLp2_[1]);

        decimatedL += 0.5f * (heldL_ * 0.35f + postLp2_[0] * 0.65f);
        decimatedR += 0.5f * (heldR_ * 0.35f + postLp2_[1] * 0.65f);
    }

    const float wetMix = std::min(1.0f, amt * 1.25f);
    left = dryL * (1.0f - wetMix) + decimatedL * wetMix;
    right = dryR * (1.0f - wetMix) + decimatedR * wetMix;
}

// ============================================================================
// 2. SampleRateDecimator (Pre-LPF + sub-sample 2-point PolyBLEP S&H edges)
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
    pendingOutL_ = 0.0f;
    pendingOutR_ = 0.0f;
    prevDryL_ = 0.0f;
    prevDryR_ = 0.0f;
    primed_ = false;
    aaIc1_.fill(0.0f);
    aaIc2_.fill(0.0f);
}

void SampleRateDecimator::processSample(float amount, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f) {
        primed_ = false;
        return;
    }

    const double factor = 1.0 + static_cast<double>(amt * amt) * 58.0;
    const double step = 1.0 / factor;

    // Band-limit ultrasonic content above the decimated Nyquist before sample-and-hold capture
    const double aaCutoffHz = std::clamp(0.55 * sampleRate_ / factor, 900.0, sampleRate_ * 0.44);
    const float g = fastTan(static_cast<float>(std::numbers::pi * aaCutoffHz / sampleRate_));
    constexpr float kButterworthDamp = 1.41421356f;
    const float a1 = 1.0f / (1.0f + g * (g + kButterworthDamp));
    const float a2 = g * a1;
    const float a3 = g * a2;

    const std::array<float, 2> rawIn = { left, right };
    std::array<float, 2> bandLimited{};
    for (size_t ch = 0; ch < 2; ++ch) {
        const float v3 = rawIn[ch] - aaIc2_[ch];
        const float v1 = a1 * aaIc1_[ch] + a2 * v3;
        const float v2 = aaIc2_[ch] + a2 * aaIc1_[ch] + a3 * v3;
        aaIc1_[ch] = 2.0f * v1 - aaIc1_[ch];
        aaIc2_[ch] = 2.0f * v2 - aaIc2_[ch];
        bandLimited[ch] = v2;
    }

    if (!primed_) {
        heldL_ = bandLimited[0];
        heldR_ = bandLimited[1];
        prevHeldL_ = heldL_;
        prevHeldR_ = heldR_;
        pendingOutL_ = heldL_;
        pendingOutR_ = heldR_;
        prevDryL_ = left;
        prevDryR_ = right;
        primed_ = true;
    }

    float outWetL = pendingOutL_;
    float outWetR = pendingOutR_;
    float nextPendingL = heldL_;
    float nextPendingR = heldR_;

    phase_ += step;
    if (phase_ >= 1.0) {
        phase_ -= 1.0;
        const float d = static_cast<float>(std::clamp(phase_ / step, 0.0, 1.0));
        prevHeldL_ = heldL_;
        prevHeldR_ = heldR_;
        heldL_ = bandLimited[0];
        heldR_ = bandLimited[1];

        const float deltaL = heldL_ - prevHeldL_;
        const float deltaR = heldR_ - prevHeldR_;
        const float preBlep = 0.5f * d * d;
        const float postBlep = 0.5f * (1.0f - d) * (1.0f - d);

        // Apply causal 2-point PolyBLEP step residuals across the sub-sample S&H clock edge
        outWetL += preBlep * deltaL;
        outWetR += preBlep * deltaR;
        nextPendingL = heldL_ - postBlep * deltaL;
        nextPendingR = heldR_ - postBlep * deltaR;
    }

    pendingOutL_ = nextPendingL;
    pendingOutR_ = nextPendingR;

    const float wet = std::min(1.0f, amt * 1.35f);
    const float alignedDryL = prevDryL_;
    const float alignedDryR = prevDryR_;
    prevDryL_ = left;
    prevDryR_ = right;

    left = alignedDryL * (1.0f - wet) + outWetL * wet;
    right = alignedDryR * (1.0f - wet) + outWetR * wet;
}

// ============================================================================
// 3. BufferStutter (Dual-head overlap-add equal-power splice crossfading)
// ============================================================================
void BufferStutter::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    const size_t maxSamples = static_cast<size_t>(sampleRate_ * 16.0) + 128;
    bufferL_.assign(maxSamples, 0.0f);
    bufferR_.assign(maxSamples, 0.0f);
    reset();
}

void BufferStutter::reset() {
    std::fill(bufferL_.begin(), bufferL_.end(), 0.0f);
    std::fill(bufferR_.begin(), bufferR_.end(), 0.0f);
    writePos_ = 0;
    postCaptureGuard_ = 0;
    hasValidSlice_ = false;
    lastReadIndex_ = 0.0;
    lastStepDelta_ = 1.0;
    smoothPanShimmer_ = 0.0f;
    outgoing_ = {};
    triggerCapture();
}

void BufferStutter::triggerCapture() {
    const int maxBuf = static_cast<int>(bufferL_.size());
    const int spliceSamples = std::max(12, static_cast<int>(std::round(0.0045 * sampleRate_)));
    if (hasValidSlice_ && !capturing_ && maxBuf > 0) {
        outgoing_.active = true;
        outgoing_.readIndex = lastReadIndex_ + lastStepDelta_;
        outgoing_.stepDelta = lastStepDelta_;
        outgoing_.xfadePos = 0;
        outgoing_.xfadeLen = spliceSamples;
        writePos_ = (loopStart_ + loopLen_ + spliceSamples + 16) % maxBuf;
    }
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
    const double safeBpm = std::clamp(bpm, 20.0, 300.0);
    const double quarterSec = 60.0 / safeBpm;

    const double maxSliceSec = std::min(0.50, quarterSec * 1.0);
    const double minSliceSec = 0.022;
    const double knobSliceSec = maxSliceSec * std::pow(minSliceSec / maxSliceSec, static_cast<double>(amt));
    const double sliceSec = settings.syncedSliceBeats > 0.0 ? settings.syncedSliceBeats * quarterSec : knobSliceSec;
    const int sliceLen = std::clamp(static_cast<int>(std::round(sliceSec * sampleRate_)), 4, maxBuf / 2 - 32);
    const int spliceLen = std::clamp(static_cast<int>(std::round(0.0045 * sampleRate_)), 4, std::max(4, sliceLen / 2));

    // Record during capture, advance mode, and a short post-slice guard tail so outgoing wrap splices read continuous audio.
    if (capturing_ || settings.advance || postCaptureGuard_ > 0) {
        bufferL_[static_cast<size_t>(writePos_)] = left;
        bufferR_[static_cast<size_t>(writePos_)] = right;
        writePos_ = (writePos_ + 1) % maxBuf;
        if (!capturing_ && !settings.advance && postCaptureGuard_ > 0) {
            --postCaptureGuard_;
        }
    }

    if (capturing_) {
        ++capturedSamples_;
        const bool finishedCapture = (capturedSamples_ >= sliceLen);
        if (finishedCapture) {
            capturing_ = false;
            hasValidSlice_ = true;
            loopLen_ = sliceLen;
            outPos_ = 0;
            repeatCycle_ = 1;
            postCaptureGuard_ = spliceLen + 8;
        }
        if (!outgoing_.active) {
            if (finishedCapture) {
                outgoing_.active = true;
                outgoing_.readIndex = static_cast<double>((loopStart_ + loopLen_) % maxBuf);
                outgoing_.stepDelta = 1.0;
                outgoing_.xfadePos = 0;
                outgoing_.xfadeLen = spliceLen;
            }
            return;
        }
        // Retrigger crossfade: equal-power splice from outgoing stutter head into live capture stream
        const float outL = readHermiteCircular(bufferL_, outgoing_.readIndex);
        const float outR = readHermiteCircular(bufferR_, outgoing_.readIndex);
        outgoing_.readIndex += outgoing_.stepDelta;
        const float theta = 0.5f * std::numbers::pi_v<float>
            * static_cast<float>(outgoing_.xfadePos + 1) / static_cast<float>(std::max(1, outgoing_.xfadeLen));
        const float gainOut = std::cos(theta);
        const float gainIn = std::sin(theta);
        if (++outgoing_.xfadePos >= outgoing_.xfadeLen) {
            outgoing_.active = false;
        }
        const float wet = std::min(1.0f, amt * 1.3f);
        const float stutL = outL * gainOut + left * gainIn;
        const float stutR = outR * gainOut + right * gainIn;
        left = left * (1.0f - wet) + stutL * wet;
        right = right * (1.0f - wet) + stutR * wet;
        return;
    }

    if (outPos_ >= loopLen_) {
        outPos_ = 0;
        ++repeatCycle_;
        if (settings.gate >= 0.999f) {
            outgoing_.active = true;
            outgoing_.readIndex = lastReadIndex_ + lastStepDelta_;
            outgoing_.stepDelta = lastStepDelta_;
            outgoing_.xfadePos = 0;
            outgoing_.xfadeLen = spliceLen;
        }
    }

    int advanceOffset = settings.advance ? (repeatCycle_ - 1) * (loopLen_ / 2) : 0;
    if (settings.advance && advanceOffset + 2 * loopLen_ >= maxBuf) {
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
    lastReadIndex_ = readIndex;
    lastStepDelta_ = reversed ? -rate : rate;

    float stutL = readHermiteCircular(bufferL_, readIndex);
    float stutR = readHermiteCircular(bufferR_, readIndex);

    // Dual-head equal-power splice crossfade on loop wraps and capture handoffs
    if (outgoing_.active) {
        const float outL = readHermiteCircular(bufferL_, outgoing_.readIndex);
        const float outR = readHermiteCircular(bufferR_, outgoing_.readIndex);
        outgoing_.readIndex += outgoing_.stepDelta;
        const float theta = 0.5f * std::numbers::pi_v<float>
            * static_cast<float>(outgoing_.xfadePos + 1) / static_cast<float>(std::max(1, outgoing_.xfadeLen));
        const float gainOut = std::cos(theta);
        const float gainIn = std::sin(theta);
        stutL = outL * gainOut + stutL * gainIn;
        stutR = outR * gainOut + stutR * gainIn;
        if (++outgoing_.xfadePos >= outgoing_.xfadeLen) {
            outgoing_.active = false;
        }
    }

    // Apply repeat gate envelope only when user lowers Buffer Gate below 100%
    if (settings.gate < 0.999f) {
        const int gateLen = std::max(8, static_cast<int>(settings.gate * static_cast<float>(loopLen_)));
        const int fadeLen = std::clamp(spliceLen, 2, std::max(2, gateLen / 4));
        const float fadeIn = std::min(1.0f, static_cast<float>(outPos_ + 1) / static_cast<float>(fadeLen));
        const float fadeOut = std::clamp(static_cast<float>(gateLen - outPos_) / static_cast<float>(fadeLen), 0.0f, 1.0f);
        const float gateWin = 0.5f - 0.5f * std::cos(std::numbers::pi_v<float> * std::min(fadeIn, fadeOut));
        stutL *= gateWin;
        stutR *= gateWin;
    }

    const float targetShimmer = (amt > 0.55f)
        ? (((repeatCycle_ & 1) != 0 ? 1.0f : -1.0f) * (amt - 0.55f) * 0.12f)
        : 0.0f;
    smoothPanShimmer_ += 0.03f * (targetShimmer - smoothPanShimmer_);
    stutL *= (1.0f - smoothPanShimmer_);
    stutR *= (1.0f + smoothPanShimmer_);

    ++outPos_;

    const float wet = std::min(1.0f, amt * 1.3f);
    left = left * (1.0f - wet) + stutL * wet;
    right = right * (1.0f - wet) + stutR * wet;
}

// ============================================================================
// 4. TapeStopEffect (Continuous circular buffer + dynamic polyphase sinc resampler)
// ============================================================================
void TapeStopEffect::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    const size_t maxSamples = static_cast<size_t>(sampleRate_ * 24.0) + 128;
    bufferL_.assign(maxSamples, 0.0f);
    bufferR_.assign(maxSamples, 0.0f);
    (void)sincTable();
    reset();
}

void TapeStopEffect::reset() {
    std::fill(bufferL_.begin(), bufferL_.end(), 0.0f);
    std::fill(bufferR_.begin(), bufferR_.end(), 0.0f);
    writePos_ = 0;
    recordedSamples_ = 0;
    lagSamples_ = 0.0;
    speed_ = 1.0;
    elapsedSamples_ = 0;
    rampSamples_ = 0.0;
    slipPhase_ = 0.0;
}

void TapeStopEffect::recordDrySample(float left, float right) {
    const int maxBuf = static_cast<int>(bufferL_.size());
    if (maxBuf <= 0) {
        return;
    }
    bufferL_[static_cast<size_t>(writePos_)] = left;
    bufferR_[static_cast<size_t>(writePos_)] = right;
    writePos_ = (writePos_ + 1) % maxBuf;
    recordedSamples_ = std::min(maxBuf, recordedSamples_ + 1);
}

void TapeStopEffect::triggerBrake() {
    lagSamples_ = 0.0;
    speed_ = 1.0;
    elapsedSamples_ = 0;
    rampSamples_ = 0.0;
}

void TapeStopEffect::processSample(float amount, double spanDurationSec, const TapeStopSettings& settings, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f || bufferL_.empty()) {
        return;
    }

    const int maxBuf = static_cast<int>(bufferL_.size());
    const int latestWrite = writePos_;
    recordDrySample(left, right);

    const bool fixedLength = settings.fixedDurationSec > 0.0;
    if (rampSamples_ == 0.0) {
        const double duration = fixedLength ? settings.fixedDurationSec : spanDurationSec;
        rampSamples_ = std::clamp(duration * sampleRate_, 1.0, 20.0 * sampleRate_);
    }
    const double minFloorSpeed = 0.14 * (1.0 - static_cast<double>(amt) * 0.86);
    double progress = std::min(1.0, static_cast<double>(elapsedSamples_) / rampSamples_);
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

    // Dynamic polyphase windowed-sinc cutoff scales with speed_ to prevent imaging/aliasing
    double baseCutoff = 0.48;
    if (settings.lowpass) baseCutoff = std::clamp(0.48 * speed_, 0.022, 0.48);
    const double sincCutoff = std::clamp(baseCutoff / std::max(1.0, speed_), 0.022, 0.48);

    const auto& table = sincTable();
    const double bandCoord = std::clamp(std::log(sincCutoff / 0.022) / std::log(0.48 / 0.022), 0.0, 1.0)
                           * static_cast<double>(PolyphaseSincTable::kCutoffs - 1);
    const int band0 = std::min(PolyphaseSincTable::kCutoffs - 2, static_cast<int>(bandCoord));
    const float bandFrac = static_cast<float>(bandCoord - static_cast<double>(band0));

    const double maxLag = static_cast<double>(std::max(0, recordedSamples_ - 1));
    const double clampedLag = std::clamp(lagSamples_, 0.0, std::min(maxLag, static_cast<double>(maxBuf - 32)));
    const double readPos = static_cast<double>(latestWrite) - clampedLag;
    const double baseFloor = std::floor(readPos);
    const double frac = readPos - baseFloor;

    const double phaseCoord = std::clamp(frac, 0.0, 1.0) * static_cast<double>(PolyphaseSincTable::kPhases);
    const int p0 = std::min(PolyphaseSincTable::kPhases - 1, static_cast<int>(phaseCoord));
    const int p1 = p0 + 1;
    const float phaseFrac = static_cast<float>(phaseCoord - static_cast<double>(p0));

    float sincL = 0.0f;
    float sincR = 0.0f;
    const int baseInt = static_cast<int>(baseFloor);
    const int maxBack = std::max(0, recordedSamples_ - 1);
    for (int k = 0; k < PolyphaseSincTable::kTaps; ++k) {
        const int tapOffset = k - PolyphaseSincTable::kCenter;
        // Clamp tap lookback to recorded causal bounds [0 .. maxBack] behind latestWrite
        const int backDist = std::clamp(latestWrite - (baseInt + tapOffset), 0, maxBack);
        const auto bufIdx = static_cast<size_t>((latestWrite - backDist + maxBuf) % maxBuf);

        const float w00 = table.weights[static_cast<size_t>(band0)][static_cast<size_t>(p0)][static_cast<size_t>(k)];
        const float w01 = table.weights[static_cast<size_t>(band0)][static_cast<size_t>(p1)][static_cast<size_t>(k)];
        const float w10 = table.weights[static_cast<size_t>(band0 + 1)][static_cast<size_t>(p0)][static_cast<size_t>(k)];
        const float w11 = table.weights[static_cast<size_t>(band0 + 1)][static_cast<size_t>(p1)][static_cast<size_t>(k)];
        const float w0 = w00 + phaseFrac * (w01 - w00);
        const float w1 = w10 + phaseFrac * (w11 - w10);
        const float w = w0 + bandFrac * (w1 - w0);

        sincL += w * bufferL_[bufIdx];
        sincR += w * bufferR_[bufIdx];
    }

    const float gainEnv = settings.fade ? static_cast<float>(std::pow(std::max(0.04, speed_), 0.36)) : 1.0f;
    const float tapeL = sincL * gainEnv;
    const float tapeR = sincR * gainEnv;

    const double maxCircularLag = static_cast<double>(std::max(1, maxBuf - 32));
    lagSamples_ = std::clamp(lagSamples_ + (1.0 - speed_), 0.0, maxCircularLag);

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
// 6. ReverseGrainEffect (Hermite fractional dual-grain + zero-crossing alignment)
// ============================================================================
void ReverseGrainEffect::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    const size_t maxBuf = static_cast<size_t>(sampleRate_ * 24.0) + 64;
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
    grainStartPos_ = 0.0;
    grainPos_ = 0.0;
    spanSamples_ = 0.0;
    lastReadPos_ = 0.0;
    outgoingReadPos_ = 0.0;
    previousSpanPhase_ = 0.0f;
    grainActive_ = false;
    outgoingActive_ = false;
    fadeFromDry_ = true;
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

double ReverseGrainEffect::alignZeroCrossing(int anchorWritePos) const noexcept {
    const int bufSize = static_cast<int>(bufferL_.size());
    if (bufSize <= 4 || recordedSamples_ < 4) {
        return static_cast<double>(anchorWritePos - 1);
    }
    const int maxSearch = std::min(64, recordedSamples_ - 2);
    for (int offset = 0; offset < maxSearch; ++offset) {
        const int i0 = (anchorWritePos - 1 - offset + bufSize) % bufSize;
        const int i1 = (i0 - 1 + bufSize) % bufSize;
        const float y0 = bufferL_[static_cast<size_t>(i0)];
        const float y1 = bufferL_[static_cast<size_t>(i1)];
        if (y0 * y1 < -1e-6f) {
            const double frac = static_cast<double>(std::abs(y0)) / static_cast<double>(std::abs(y0) + std::abs(y1));
            return static_cast<double>(anchorWritePos - 1 - offset) - frac;
        }
    }
    return static_cast<double>(anchorWritePos - 1);
}

void ReverseGrainEffect::triggerGrain() {
    if (grainActive_) {
        outgoingActive_ = true;
        outgoingReadPos_ = lastReadPos_ - 1.0;
    }
    grainStartPos_ = alignZeroCrossing(writePos_);
    preRollSamples_ = recordedSamples_;
    grainPos_ = 0.0;
    spanSamples_ = 0.0;
    previousSpanPhase_ = 0.0f;
}

void ReverseGrainEffect::processSample(float amount, float spanPhase01, double spanDurationSec, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    const int bufSize = static_cast<int>(bufferL_.size());
    if (amt <= 1e-4f || bufSize == 0) {
        return;
    }

    const double maxSpan = static_cast<double>(std::max(1, bufSize / 2 - 4));
    const double currentSpanSamples = std::clamp(spanDurationSec * sampleRate_, 1.0, maxSpan);
    const bool wrapped = previousSpanPhase_ > 0.9f && spanPhase01 < 0.1f;
    const bool heldNoteCycle = spanPhase01 >= 1.0f && grainPos_ >= spanSamples_;
    if (grainPos_ > 0.0 && (wrapped || heldNoteCycle)) {
        triggerGrain();
    }
    previousSpanPhase_ = spanPhase01;
    spanSamples_ = std::max(spanSamples_, currentSpanSamples);

    const double position = std::min(grainPos_, std::max(0.0, spanSamples_ - 1.0));
    if (grainPos_ < spanSamples_) {
        grainPos_ += 1.0;
    }

    // A causal reverse needs a complete preceding region; never loop a short cold-start fragment.
    if (static_cast<double>(preRollSamples_) + 0.5 < spanSamples_) {
        grainActive_ = false;
        outgoingActive_ = false;
        fadeFromDry_ = true;
        return;
    }

    const double readPos = grainStartPos_ - position;
    lastReadPos_ = readPos;
    grainActive_ = true;

    float revL = readHermiteCircular(bufferL_, readPos);
    float revR = readHermiteCircular(bufferR_, readPos);

    // Overlapping raised-cosine dual-grain crossfade at grain start/wrap
    const double xfadeLen = std::clamp(0.006 * sampleRate_, 2.0, std::max(2.0, spanSamples_ / 8.0));
    if (position < xfadeLen && (outgoingActive_ || fadeFromDry_)) {
        const float phase = static_cast<float>(std::clamp((position + 1.0) / xfadeLen, 0.0, 1.0));
        const float wIn = 0.5f * (1.0f - std::cos(std::numbers::pi_v<float> * phase));
        const float wOut = 1.0f - wIn;
        float tailL = left;
        float tailR = right;
        if (outgoingActive_) {
            tailL = readHermiteCircular(bufferL_, outgoingReadPos_);
            tailR = readHermiteCircular(bufferR_, outgoingReadPos_);
        }
        if (outgoingActive_) {
            outgoingReadPos_ -= 1.0;
        }
        revL = revL * wIn + tailL * wOut;
        revR = revR * wIn + tailR * wOut;
    }
    if (position >= xfadeLen) {
        outgoingActive_ = false;
        fadeFromDry_ = false;
    }

    const float wet = std::min(1.0f, amt * 1.25f);
    left = left * (1.0f - wet) + revL * wet;
    right = right * (1.0f - wet) + revR * wet;
}

// ============================================================================
// 7. ChopperGateEffect
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

    const double safeBpm = std::clamp(bpm, 20.0, 300.0);
    const double spanScale = 1.0 / std::sqrt(static_cast<double>(std::clamp(spanLengthSteps, 1, 32)));
    const double baseHz = (safeBpm / 60.0) * (1.5 + static_cast<double>(amt) * 10.5) * spanScale;
    phase_ += baseHz / sampleRate_;
    if (phase_ >= 1.0) {
        phase_ -= 1.0;
    }

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
// 8. PitchGlitchEffect (WSOLA short transient grains + +/-128 cross-correlated splice locking)
// ============================================================================
void PitchGlitchEffect::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    const size_t bufSize = static_cast<size_t>(sampleRate_ * 0.25) + 512;
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
    grainLenSamples_ = std::clamp(0.018 * sampleRate_, 96.0, 2048.0);
    readPos_.fill(0.0);
    headInitialized_.fill(false);
    currentRatio_ = 1.0;
    fastEnv_ = 0.0f;
    slowEnv_ = 0.0f;
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

    const float level = 0.5f * (std::abs(left) + std::abs(right));
    const float fastCoeff = level > fastEnv_ ? 0.25f : 0.008f;
    fastEnv_ += fastCoeff * (level - fastEnv_);
    slowEnv_ += 0.0015f * (level - slowEnv_);
}

double PitchGlitchEffect::findWsolaSpliceStart(int latestWrite,
                                               double nominalStart,
                                               double refPos,
                                               double grainLen) const noexcept {
    const int bufSize = static_cast<int>(bufferL_.size());
    if (bufSize <= 64 || recordedSamples_ < 96) {
        return nominalStart;
    }

    constexpr int kSearchRadius = 128;
    constexpr int kTemplatePoints = 24;
    constexpr int kTemplateStride = 2;
    const int templateSpan = kTemplatePoints * kTemplateStride;
    const double netAdvance = std::max(0.0, (currentRatio_ - 1.0) * grainLen);
    const double maxSafeStart = static_cast<double>(latestWrite) - netAdvance - 4.0;
    const double minSafeStart = static_cast<double>(latestWrite - recordedSamples_ + 4);

    // Coarse-to-fine normalized cross-correlation over [-128, +128] samples
    const int refBase = static_cast<int>(std::floor(refPos));
    float refEnergy = 0.0f;
    for (int k = 0; k < kTemplatePoints; ++k) {
        const int idx = ((refBase + k * kTemplateStride) % bufSize + bufSize) % bufSize;
        const float s = bufferL_[static_cast<size_t>(idx)] + bufferR_[static_cast<size_t>(idx)];
        refEnergy += s * s;
    }
    if (refEnergy < 1e-8f) {
        return std::clamp(nominalStart, minSafeStart, maxSafeStart);
    }

    auto scoreCandidate = [&](int delta) noexcept -> float {
        const double cand = nominalStart + static_cast<double>(delta);
        if (cand < minSafeStart || cand + static_cast<double>(templateSpan) > maxSafeStart + netAdvance) {
            return -2.0f;
        }
        const int candBase = static_cast<int>(std::floor(cand));
        float dot = 0.0f;
        float candEnergy = 0.0f;
        for (int k = 0; k < kTemplatePoints; ++k) {
            const int rIdx = ((refBase + k * kTemplateStride) % bufSize + bufSize) % bufSize;
            const int cIdx = ((candBase + k * kTemplateStride) % bufSize + bufSize) % bufSize;
            const float rSample = bufferL_[static_cast<size_t>(rIdx)] + bufferR_[static_cast<size_t>(rIdx)];
            const float cSample = bufferL_[static_cast<size_t>(cIdx)] + bufferR_[static_cast<size_t>(cIdx)];
            dot += rSample * cSample;
            candEnergy += cSample * cSample;
        }
        return dot / std::sqrt(refEnergy * candEnergy + 1e-8f);
    };

    int bestCoarse = 0;
    float bestScore = -2.0f;
    for (int d = -kSearchRadius; d <= kSearchRadius; d += 4) {
        const float s = scoreCandidate(d);
        if (s > bestScore) {
            bestScore = s;
            bestCoarse = d;
        }
    }

    int bestDelta = bestCoarse;
    const int fineLo = std::max(-kSearchRadius, bestCoarse - 3);
    const int fineHi = std::min(kSearchRadius, bestCoarse + 3);
    for (int d = fineLo; d <= fineHi; ++d) {
        const float s = scoreCandidate(d);
        if (s > bestScore) {
            bestScore = s;
            bestDelta = d;
        }
    }

    return std::clamp(nominalStart + static_cast<double>(bestDelta), minSafeStart, maxSafeStart);
}

void PitchGlitchEffect::processSample(float amount, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f || bufferL_.empty()) {
        return;
    }

    const int latestWrite = writePos_ - 1;
    const int hopPeriod = std::max(4096, static_cast<int>(sampleRate_ * (0.50 - 0.25 * static_cast<double>(amt))));
    if (++stepCounter_ >= hopPeriod) {
        stepCounter_ = 0;
        intervalIndex_ = (intervalIndex_ + 1) & 3;
    }

    // +7, +12, -12 and +19 semitones
    static constexpr std::array<double, 4> kRatios = { 1.498307, 2.0, 0.5, 2.996614 };
    const double targetRatio = 1.0 + (kRatios[static_cast<size_t>(intervalIndex_)] - 1.0) * static_cast<double>(amt);
    currentRatio_ += 0.015 * (targetRatio - currentRatio_);

    // Short 13 ms grains on percussive transients, 19 ms grains on steady tonal basslines
    const bool isTransient = fastEnv_ > (1.5f * slowEnv_ + 0.004f);
    const double targetGrainSec = isTransient ? 0.013 : 0.019;
    const double maxSafeGrain = static_cast<double>(std::max(32, std::min(recordedSamples_ / 2, 1200)));
    const double desiredGrain = std::clamp(targetGrainSec * sampleRate_, 32.0, maxSafeGrain);
    grainLenSamples_ += 0.05 * (desiredGrain - grainLenSamples_);
    const double grainLen = std::max(32.0, grainLenSamples_);

    const double prevPhase0 = grainPhase_;
    const double prevPhase1 = std::fmod(prevPhase0 + 0.5, 1.0);
    grainPhase_ += 1.0 / grainLen;
    const bool wrap0 = grainPhase_ >= 1.0;
    if (wrap0) {
        grainPhase_ -= std::floor(grainPhase_);
    }
    const double phase1 = std::fmod(grainPhase_ + 0.5, 1.0);
    const bool wrap1 = phase1 < prevPhase1;

    const double netAdvance = std::max(0.0, (currentRatio_ - 1.0) * grainLen);
    const double searchPad = std::min(128.0, static_cast<double>(recordedSamples_) * 0.25);
    const double nominalLag = std::min(static_cast<double>(std::max(8, recordedSamples_ - 6)),
                                       netAdvance + searchPad + 8.0);
    const double nominalStart = static_cast<double>(latestWrite) - nominalLag;

    if (!headInitialized_[0]) {
        readPos_[0] = nominalStart;
        readPos_[1] = nominalStart - 0.5 * grainLen * (currentRatio_ - 1.0);
        headInitialized_[0] = true;
        headInitialized_[1] = true;
    }
    if (wrap0) {
        readPos_[0] = findWsolaSpliceStart(latestWrite, nominalStart, readPos_[1], grainLen);
    }
    if (wrap1) {
        readPos_[1] = findWsolaSpliceStart(latestWrite, nominalStart, readPos_[0], grainLen);
    }

    const float win0 = static_cast<float>(0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * grainPhase_)));
    const float win1 = static_cast<float>(0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * phase1)));

    const float pitchedL = readHermiteCircular(bufferL_, readPos_[0]) * win0
                         + readHermiteCircular(bufferL_, readPos_[1]) * win1;
    const float pitchedR = readHermiteCircular(bufferR_, readPos_[0]) * win0
                         + readHermiteCircular(bufferR_, readPos_[1]) * win1;

    readPos_[0] += currentRatio_;
    readPos_[1] += currentRatio_;

    const float wet = std::min(1.0f, amt * 1.2f);
    left = left * (1.0f - wet) + pitchedL * wet;
    right = right * (1.0f - wet) + pitchedR * wet;
}

// ============================================================================
// 9. CombResonatorEffect (with tail-preserving send gating)
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
    tailEnergy_ = 0.0f;
}

void CombResonatorEffect::processSample(float amount, float& left, float& right) {
    processSendSample(amount, 1.0f, left, right);
}

void CombResonatorEffect::processSendSample(float amount, float sendGate, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    const float gate = std::clamp(sendGate, 0.0f, 1.0f);
    if (amt <= 1e-4f || delayL_.empty()) {
        return;
    }

    const int bufSize = static_cast<int>(delayL_.size());
    const double delaySec = 0.014 * std::pow(0.0014 / 0.014, static_cast<double>(amt));
    const double targetDelay = std::clamp(delaySec * sampleRate_, 6.0, static_cast<double>(bufSize - 4));
    smoothDelaySamples_ += 0.008 * (targetDelay - smoothDelaySamples_);

    const double readPosL = static_cast<double>(writePos_) - smoothDelaySamples_;
    const double readPosR = static_cast<double>(writePos_) - smoothDelaySamples_ * (1.0 + 0.018 * static_cast<double>(amt));

    const float dL = readHermiteCircular(delayL_, readPosL);
    const float dR = readHermiteCircular(delayR_, readPosR);

    const float dampCoeff = 0.72f + 0.18f * amt;
    dampL_ += dampCoeff * (dL - dampL_);
    dampR_ += dampCoeff * (dR - dampR_);

    const float fb = 0.48f + amt * 0.42f;
    const float exciteL = left * gate;
    const float exciteR = right * gate;
    const float nextL = std::tanh(exciteL + dampL_ * fb);
    const float nextR = std::tanh(exciteR + dampR_ * fb);

    delayL_[static_cast<size_t>(writePos_)] = nextL;
    delayR_[static_cast<size_t>(writePos_)] = nextR;
    writePos_ = (writePos_ + 1) % bufSize;

    tailEnergy_ = std::max(nextL * nextL + nextR * nextR, tailEnergy_ * 0.995f);

    const float wet = std::min(1.0f, amt * 1.15f);
    const float dryDuck = 1.0f - wet * gate;
    left = left * dryDuck + nextL * wet;
    right = right * dryDuck + nextR * wet;
}

// ============================================================================
// 10. WavefolderEffect
// ============================================================================
void WavefolderEffect::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    dcPole_ = static_cast<float>(std::pow(0.995, 44100.0 / sampleRate_));
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

    const double drive = 1.0 + static_cast<double>(amt) * 9.0;
    const double depth = 0.32 * static_cast<double>(amt);
    const auto fold = [&](float input, double& previous) {
        const double x = input;
        const double delta = x - previous;
        const double uMid = drive * 0.5 * (x + previous);
        double folded = (1.0 + depth / 2.0) * std::sin(uMid) + depth / 2.0 * std::sin(3.0 * uMid);
        if (std::abs(delta) >= 1.0e-6) {
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

    dcOutL_ = foldL - dcInL_ + dcPole_ * dcOutL_;
    dcInL_ = foldL;
    dcOutR_ = foldR - dcInR_ + dcPole_ * dcOutR_;
    dcInR_ = foldR;

    const float toneAlpha = 0.82f - 0.18f * amt;
    toneLpL_ += toneAlpha * (dcOutL_ - toneLpL_);
    toneLpR_ += toneAlpha * (dcOutR_ - toneLpR_);

    const float wet = std::min(1.0f, amt * 1.15f);
    left = left * (1.0f - wet) + toneLpL_ * wet;
    right = right * (1.0f - wet) + toneLpR_ * wet;
}

// ============================================================================
// Stereo Thiran dispersive modulation, 12-pole ZDF Phaser, SVF PingPongEcho, Saturation
// ============================================================================
void ModulatedDelayEffect::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    for (auto& channel : delay_) {
        channel.assign(static_cast<size_t>(sampleRate_ * 0.05) + 16, 0.0f);
    }
    reset();
}

void ModulatedDelayEffect::reset() {
    for (auto& channel : delay_) {
        std::fill(channel.begin(), channel.end(), 0.0f);
    }
    phase_ = 0.0;
    writePos_ = 0;
    for (auto& s : thiranState_) s.fill(0.0f);
    for (auto& s : thiranTap2State_) s.fill(0.0f);
    for (auto& s : dispState_) s.fill(0.0f);
    fbLp1_.fill(0.0f);
    fbLp2_.fill(0.0f);
    fbHpIn_.fill(0.0f);
    fbHpOut_.fill(0.0f);
}

void ModulatedDelayEffect::processSample(Mode mode, float amount, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f || delay_[0].empty()) {
        return;
    }
    const bool flanger = mode == Mode::Flanger;
    const double rate = flanger ? 0.08 + 0.52 * amt : 0.15 + 0.85 * amt;
    phase_ += rate / sampleRate_;
    phase_ -= std::floor(phase_);

    const float lpAlpha = static_cast<float>(1.0 - std::exp(-2.0 * std::numbers::pi * 5500.0 / sampleRate_));
    const float hpPole = static_cast<float>(std::exp(-2.0 * std::numbers::pi * 95.0 / sampleRate_));
    const float dispG = fastTan(static_cast<float>(std::numbers::pi * std::min(sampleRate_ * 0.4, 2400.0) / sampleRate_));
    const float dispCoeff = dispG / (1.0f + dispG);

    const std::array<float, 2> input = { left, right };
    std::array<float, 2> output{};
    for (size_t ch = 0; ch < 2; ++ch) {
        const double angle = 2.0 * std::numbers::pi * (phase_ + 0.25 * ch);
        const double delaySec = flanger ? 0.003 + 0.0025 * amt * std::sin(angle)
                                        : 0.018 + 0.006 * amt * std::sin(angle);
        const double offset = std::max(3.0, sampleRate_ * delaySec);

        // True click-free dual-staggered Thiran allpass fractional delay interpolation
        float delayed = readThiranDualHead(delay_[ch], writePos_, offset, thiranState_[ch]);
        if (!flanger) {
            const double secondOffset = std::max(3.0, sampleRate_ * (0.027 + 0.005 * amt * std::sin(angle + 2.1)));
            delayed = 0.5f * (delayed + readThiranDualHead(delay_[ch], writePos_, secondOffset, thiranTap2State_[ch]));
        }

        // 2-stage ZDF dispersive allpass in the wet modulation path
        for (size_t st = 0; st < 2; ++st) {
            const float v = (delayed - dispState_[ch][st]) * dispCoeff;
            const float lp = v + dispState_[ch][st];
            dispState_[ch][st] = lp + v;
            delayed = 2.0f * lp - delayed;
        }

        // Multi-pole feedback shelving (95 Hz HPF + 2-pole 5.5 kHz LPF + tanh saturation)
        fbHpOut_[ch] = delayed - fbHpIn_[ch] + hpPole * fbHpOut_[ch];
        fbHpIn_[ch] = delayed;
        fbLp1_[ch] += lpAlpha * (fbHpOut_[ch] - fbLp1_[ch]);
        fbLp2_[ch] += lpAlpha * (fbLp1_[ch] - fbLp2_[ch]);

        const float feedback = flanger ? 0.72f * amt : 0.18f * amt;
        const float fbSignal = std::tanh(fbLp2_[ch] * feedback);
        delay_[ch][static_cast<size_t>(writePos_)] = input[ch] + fbSignal;

        const float mix = 0.5f * amt;
        output[ch] = input[ch] * (1.0f - mix) + delayed * mix;
    }
    writePos_ = (writePos_ + 1) % static_cast<int>(delay_[0].size());
    left = output[0];
    right = output[1];
}

void PhaserEffect::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    reset();
}

void PhaserEffect::reset() {
    for (auto& channel : state_) {
        channel.fill(0.0f);
    }
    feedback_.fill(0.0f);
    fbHpState_.fill(0.0f);
    fbLpState_.fill(0.0f);
    phase_ = 0.0;
}

void PhaserEffect::processSample(float amount, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f) {
        return;
    }
    phase_ += (0.08 + 1.15 * amt) / sampleRate_;
    phase_ -= std::floor(phase_);

    const float hpG = fastTan(static_cast<float>(std::numbers::pi * 85.0 / sampleRate_)) / (1.0f + fastTan(static_cast<float>(std::numbers::pi * 85.0 / sampleRate_)));
    const float lpG = fastTan(static_cast<float>(std::numbers::pi * std::min(sampleRate_ * 0.42, 7500.0) / sampleRate_));
    const float lpAlpha = lpG / (1.0f + lpG);
    const float stage12Mix = std::clamp((amt - 0.15f) / 0.50f, 0.0f, 1.0f);

    const std::array<float, 2> input = { left, right };
    std::array<float, 2> output{};
    for (size_t ch = 0; ch < 2; ++ch) {
        const double sweep = 0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * (phase_ + 0.15 * ch));
        const double centerHz = 180.0 * std::pow(14.0, sweep);

        // Band-shelved tanh feedback resonance prevents DC rumble and Nyquist oscillation
        const float fbHpV = (feedback_[ch] - fbHpState_[ch]) * hpG;
        const float fbHpLp = fbHpV + fbHpState_[ch];
        fbHpState_[ch] = fbHpLp + fbHpV;
        const float fbHp = feedback_[ch] - fbHpLp;
        const float fbLpV = (fbHp - fbLpState_[ch]) * lpAlpha;
        const float fbBand = fbLpV + fbLpState_[ch];
        fbLpState_[ch] = fbBand + fbLpV;

        float value = input[ch] + 0.68f * amt * std::tanh(fbBand * 1.15f);
        float tap6 = value;

        // 12 cascaded ZDF (TPT) 1st-order dispersive allpass stages spread across ~4.4 octaves
        for (size_t stage = 0; stage < kNumStages; ++stage) {
            const double spread = 0.22 * std::pow(22.0, static_cast<double>(stage) / static_cast<double>(kNumStages - 1));
            const double stageHz = std::clamp(centerHz * spread, 30.0, sampleRate_ * 0.44);
            const float g = fastTan(static_cast<float>(std::numbers::pi * stageHz / sampleRate_));
            const float G = g / (1.0f + g);
            const float v = (value - state_[ch][stage]) * G;
            const float lp = v + state_[ch][stage];
            state_[ch][stage] = lp + v;
            value = 2.0f * lp - value;
            if (stage == 5) {
                tap6 = value;
            }
        }

        const float stagedWet = tap6 * (1.0f - stage12Mix) + value * stage12Mix;
        feedback_[ch] = stagedWet;
        output[ch] = input[ch] * (1.0f - 0.5f * amt) + stagedWet * (0.5f * amt);
    }
    left = output[0];
    right = output[1];
}

void PingPongEchoEffect::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    morphStep_ = static_cast<float>(1.0 / std::max(16.0, 0.035 * sampleRate_));
    for (auto& channel : delay_) {
        channel.assign(static_cast<size_t>(sampleRate_ * 2.0) + 16, 0.0f);
    }
    reset();
}

void PingPongEchoEffect::reset() {
    for (auto& channel : delay_) {
        std::fill(channel.begin(), channel.end(), 0.0f);
    }
    hpIc1_.fill(0.0f);
    hpIc2_.fill(0.0f);
    lpIc1_.fill(0.0f);
    lpIc2_.fill(0.0f);
    writePos_ = 0;
    activeDelaySamples_ = 0.0;
    prevDelaySamples_ = 0.0;
    targetDelaySamples_ = 0.0;
    morphPhase_ = 1.0f;
    for (auto& s : thiranAct_) s.fill(0.0f);
    for (auto& s : thiranPrev_) s.fill(0.0f);
}

void PingPongEchoEffect::processSample(float amount, double bpm, float& left, float& right) {
    processSendSample(amount, bpm, 1.0f, left, right);
}

void PingPongEchoEffect::processSendSample(float amount, double bpm, float sendGate, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    const float gate = std::clamp(sendGate, 0.0f, 1.0f);
    if (amt <= 1e-4f || delay_[0].empty()) {
        return;
    }

    const int bufSize = static_cast<int>(delay_[0].size());
    const double target = std::clamp(sampleRate_ * 30.0 / std::clamp(bpm, 20.0, 300.0), 4.0, static_cast<double>(bufSize - 8));
    if (activeDelaySamples_ <= 0.0) {
        activeDelaySamples_ = target;
        prevDelaySamples_ = target;
        targetDelaySamples_ = target;
        morphPhase_ = 1.0f;
    }
    targetDelaySamples_ = target;
    if (morphPhase_ >= 1.0f && std::abs(targetDelaySamples_ - activeDelaySamples_) > 0.5) {
        prevDelaySamples_ = activeDelaySamples_;
        activeDelaySamples_ = targetDelaySamples_;
        morphPhase_ = 0.0f;
    }

    float rawL = readThiranDualHead(delay_[0], writePos_, activeDelaySamples_, thiranAct_[0]);
    float rawR = readThiranDualHead(delay_[1], writePos_, activeDelaySamples_, thiranAct_[1]);
    if (morphPhase_ < 1.0f) {
        morphPhase_ = std::min(1.0f, morphPhase_ + morphStep_);
        const float theta = 0.5f * std::numbers::pi_v<float> * morphPhase_;
        const float gPrev = std::cos(theta);
        const float gAct = std::sin(theta);
        const float prevL = readThiranDualHead(delay_[0], writePos_, prevDelaySamples_, thiranPrev_[0]);
        const float prevR = readThiranDualHead(delay_[1], writePos_, prevDelaySamples_, thiranPrev_[1]);
        rawL = prevL * gPrev + rawL * gAct;
        rawR = prevR * gPrev + rawR * gAct;
    }

    // 120 Hz ZDF SVF High-Pass + 7 kHz ZDF SVF Low-Pass echo conditioning
    constexpr float kSvfDamp = 1.41421356f;
    const float gHp = fastTan(static_cast<float>(std::numbers::pi * std::min(120.0, sampleRate_ * 0.2) / sampleRate_));
    const float hpA1 = 1.0f / (1.0f + gHp * (gHp + kSvfDamp));
    const float hpA2 = gHp * hpA1;
    const float hpA3 = gHp * hpA2;

    const bool applyLp = sampleRate_ > 15000.0;
    float gLp = 0.0f;
    if (applyLp) gLp = fastTan(static_cast<float>(std::numbers::pi * std::min(7000.0, sampleRate_ * 0.44) / sampleRate_));
    float lpA1 = 1.0f;
    if (applyLp) lpA1 = 1.0f / (1.0f + gLp * (gLp + kSvfDamp));
    const float lpA2 = gLp * lpA1;
    const float lpA3 = gLp * lpA2;

    std::array<float, 2> filt = { rawL, rawR };
    for (size_t ch = 0; ch < 2; ++ch) {
        const float v0 = filt[ch];
        const float v3 = v0 - hpIc2_[ch];
        const float v1 = hpA1 * hpIc1_[ch] + hpA2 * v3;
        const float v2 = hpIc2_[ch] + hpA2 * hpIc1_[ch] + hpA3 * v3;
        hpIc1_[ch] = 2.0f * v1 - hpIc1_[ch];
        hpIc2_[ch] = 2.0f * v2 - hpIc2_[ch];
        float band = v0 - kSvfDamp * v1 - v2;

        if (applyLp) {
            const float lv3 = band - lpIc2_[ch];
            const float lv1 = lpA1 * lpIc1_[ch] + lpA2 * lv3;
            const float lv2 = lpIc2_[ch] + lpA2 * lpIc1_[ch] + lpA3 * lv3;
            lpIc1_[ch] = 2.0f * lv1 - lpIc1_[ch];
            lpIc2_[ch] = 2.0f * lv2 - lpIc2_[ch];
            band = lv2;
        }
        filt[ch] = band;
    }

    const float feedback = 0.2f + 0.55f * amt;
    const float s0 = std::tanh(filt[0]);
    const float s1 = std::tanh(filt[1]);
    const float sat0 = s0 + 0.04f * s0 * std::abs(s0);
    const float sat1 = s1 + 0.04f * s1 * std::abs(s1);

    const float sendMono = 0.5f * (left + right) * gate;
    delay_[0][static_cast<size_t>(writePos_)] = sendMono + feedback * sat1;
    delay_[1][static_cast<size_t>(writePos_)] = feedback * sat0;
    writePos_ = (writePos_ + 1) % bufSize;

    const float dryGain = 1.0f - 0.4f * amt * gate;
    left = left * dryGain + filt[0] * 0.6f * amt;
    right = right * dryGain + filt[1] * 0.6f * amt;
}

void SaturationEffect::prepare(double sampleRate) {
    dcPole_ = static_cast<float>(std::exp(-2.0 * std::numbers::pi * 15.0 / std::max(1000.0, sampleRate)));
    reset();
}

void SaturationEffect::reset() {
    previous_.fill(0.0);
    dcIn_.fill(0.0f);
    dcOut_.fill(0.0f);
}

void SaturationEffect::processSample(float amount, float& left, float& right) {
    const float amt = std::clamp(amount, 0.0f, 1.0f);
    if (amt <= 1e-4f) {
        return;
    }
    const double drive = 1.0 + 5.0 * amt;
    const double gain = 1.0 / std::sqrt(drive);
    const auto antiderivative = [](double x) {
        const double a = std::abs(x);
        return a + std::log1p(std::exp(-2.0 * a));
    };
    auto tick = [&](float input, size_t ch) {
        const double x = drive * input;
        const double delta = x - previous_[ch];
        const double shaped = std::abs(delta) < 1e-5 ? std::tanh(0.5 * (x + previous_[ch]))
            : (antiderivative(x) - antiderivative(previous_[ch])) / delta;
        previous_[ch] = x;
        const float wet = static_cast<float>(gain * shaped);
        dcOut_[ch] = wet - dcIn_[ch] + dcPole_ * dcOut_[ch];
        dcIn_[ch] = wet;
        return input * (1.0f - amt) + dcOut_[ch] * amt;
    };
    left = tick(left, 0);
    right = tick(right, 1);
}

// ============================================================================
// WetFilter (Per-sample log-smoothed trapezoidal ZDF SVF with fast polynomial tan)
// ============================================================================
void WetFilter::prepare(double sampleRate) {
    sampleRate_ = std::max(1000.0, sampleRate);
    coeffSmoothAlpha_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.0025 * sampleRate_)));
    reset();
}

void WetFilter::reset() {
    ic1_.fill(0.0f);
    ic2_.fill(0.0f);
    initialized_ = false;
}

void WetFilter::processSample(FilterMode mode, float cutoffHz, float resonance01, float& left, float& right) {
    if (mode == FilterMode::Off) {
        return;
    }

    const float nyquistSafe = static_cast<float>(sampleRate_ * 0.45);
    const float clampedCutoff = std::clamp(cutoffHz, 20.0f, nyquistSafe);
    const float targetLogCutoff = std::log2(clampedCutoff);
    const float targetReso = std::clamp(resonance01, 0.0f, 1.0f);

    if (!initialized_) {
        smoothLogCutoff_ = targetLogCutoff;
        smoothResonance_ = targetReso;
        initialized_ = true;
    }
    smoothLogCutoff_ += coeffSmoothAlpha_ * (targetLogCutoff - smoothLogCutoff_);
    smoothResonance_ += coeffSmoothAlpha_ * (targetReso - smoothResonance_);

    const float fc = std::exp2(smoothLogCutoff_);
    const float g = fastTan(std::numbers::pi_v<float> * fc / static_cast<float>(sampleRate_));
    const float k = 2.0f - 1.9f * smoothResonance_;
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;

    std::array<float, 2> chVal = { left, right };
    for (size_t ch = 0; ch < 2; ++ch) {
        const float v0 = chVal[ch];
        const float v3 = v0 - ic2_[ch];
        const float v1 = a1 * ic1_[ch] + a2 * v3;
        const float v2 = ic2_[ch] + a2 * ic1_[ch] + a3 * v3;
        ic1_[ch] = 2.0f * v1 - ic1_[ch];
        ic2_[ch] = 2.0f * v2 - ic2_[ch];
        chVal[ch] = (mode == FilterMode::LowPass) ? v2 : (v0 - k * v1 - v2);
    }

    left = chVal[0];
    right = chVal[1];
}

} // namespace openglitch::core
