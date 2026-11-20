#include "../Source/Core/GlitchProcessorCore.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>
#include <vector>

using namespace openglitch::core;

namespace {

void fillSineStereo(std::vector<float>& left, std::vector<float>& right, double sampleRate, double freqHz, float amp = 0.6f) {
    for (size_t i = 0; i < left.size(); ++i) {
        const float v = amp * std::sin(static_cast<float>(2.0 * std::numbers::pi * freqHz * static_cast<double>(i) / sampleRate));
        left[i] = v;
        right[i] = v * 0.9f;
    }
}

float computeRmsDifference(const std::vector<float>& a, const std::vector<float>& b) {
    double sumSq = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sumSq += d * d;
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(a.size())));
}

} // namespace

TEST_CASE("Reverse reads the whole region backwards without short loops", "[core][reverse][span]") {
    for (double duration : { 0.025, 0.5, 6.0, 12.0 }) {
        INFO("Span seconds " << duration);
        constexpr double sr = 8000;
        const int length = static_cast<int>(sr * duration);
        ReverseGrainEffect reverse;
        reverse.prepare(sr);
        for (int i = 0; i < length; ++i) reverse.recordDrySample(0.2f + 0.6f * i / length, -0.2f - 0.6f * i / length);
        reverse.triggerGrain();
        for (int i = 0; i < length; ++i) {
            reverse.recordDrySample(-0.9f, 0.9f);
            float l = 0, r = 0;
            reverse.processSample(1, static_cast<float>(i) / length, duration, l, r);
            if (i > length / 8 && i < length * 7 / 8) {
                const float expected = 0.2f + 0.6f * (length - 1 - i) / length;
                REQUIRE_THAT(l, Catch::Matchers::WithinAbs(expected, 1e-5));
                REQUIRE_THAT(r, Catch::Matchers::WithinAbs(-expected, 1e-5));
            }
        }
    }
}

TEST_CASE("Cold reverse captures a complete span instead of repeating tiny grains", "[core][reverse][span]") {
    ReverseGrainEffect reverse;
    reverse.prepare(8000);
    reverse.triggerGrain();
    for (int i = 0; i < 4000; ++i) {
        const float dry = 0.2f + 0.6f * i / 4000;
        float l = dry, r = -dry;
        reverse.processSample(1, static_cast<float>(i) / 4000, 0.5, l, r);
        reverse.recordDrySample(dry, -dry);
        REQUIRE(l == dry);
        REQUIRE(r == -dry);
    }
    for (int i = 0; i < 4000; ++i) {
        float l = 0, r = 0;
        reverse.processSample(1, static_cast<float>(i) / 4000, 0.5, l, r);
        reverse.recordDrySample(0, 0);
        if (i > 500 && i < 3500) REQUIRE_THAT(l, Catch::Matchers::WithinAbs(0.2 + 0.6 * (3999 - i) / 4000, 1e-5));
    }
}

TEST_CASE("GlitchProcessorCore passes dry audio when current beat step is disabled", "[core]") {
    constexpr double kSr = 48000.0;
    constexpr int kN = 512;

    GlitchProcessorCore core;
    core.prepare(kSr);
    core.setGateSmoothingMs(0.1f);
    core.applyPatternPreset(StepPatternPreset::LastBeat); // Only steps 24..31 active
    core.setEffectEnabled(GlitchFxId::Bitcrush, true);
    core.setEffectAmount(GlitchFxId::Bitcrush, 0.9f);

    // Position at PPQ = 0.0 (Step 0 -> disabled)
    core.setTransport(120.0, true, true, 0.0);

    std::vector<float> dryL(kN), dryR(kN);
    fillSineStereo(dryL, dryR, kSr, 440.0);
    auto procL = dryL;
    auto procR = dryR;

    core.processBlock(procL.data(), procR.data(), kN);

    for (int i = 64; i < kN; ++i) {
        REQUIRE_THAT(procL[static_cast<size_t>(i)], Catch::Matchers::WithinAbs(dryL[static_cast<size_t>(i)], 1e-4f));
        REQUIRE_THAT(procR[static_cast<size_t>(i)], Catch::Matchers::WithinAbs(dryR[static_cast<size_t>(i)], 1e-4f));
    }
}

TEST_CASE("Every FX modifies audio when routed and passes dry audio at zero amount", "[core][fx]") {
    constexpr double kSr = 48000.0;

    for (int index = 0; index < 15; ++index) {
        const auto fx = static_cast<GlitchFxId>(index);
        GlitchProcessorCore core;
        core.prepare(kSr);
        core.applyPatternPreset(StepPatternPreset::AllOn);

        for (int i = 0; i < GlitchProcessorCore::kNumEffects; ++i) {
            core.setEffectAmount(static_cast<GlitchFxId>(i), 0.0f);
        }
        core.setEffectEnabled(fx, true);
        core.setEffectAmount(fx, 0.85f);
        core.setAllSteps(false);
        core.setGateSmoothingMs(0);
        std::vector<float> historyL(96000), historyR(96000);
        fillSineStereo(historyL, historyR, kSr, 217.0);
        core.setTransport(120, true, true, -4);
        core.processBlock(historyL.data(), historyR.data(), static_cast<int>(historyL.size()));
        core.setAllSteps(true);
        core.setTransport(120, true, true, 0);
        std::vector<float> dryL(24000), dryR(24000);
        fillSineStereo(dryL, dryR, kSr, 330.0);
        auto wetL = dryL;
        auto wetR = dryR;
        core.processBlock(wetL.data(), wetR.data(), static_cast<int>(wetL.size()));

        const float diff = computeRmsDifference(dryL, wetL);
        INFO("Testing FX ID: " << static_cast<int>(fx));
        REQUIRE(diff > 0.01f);
        core.setEffectAmount(fx, 0);
        wetL = dryL;
        wetR = dryR;
        core.processBlock(wetL.data(), wetR.data(), static_cast<int>(wetL.size()));
        REQUIRE(computeRmsDifference(dryL, wetL) < 1e-6f);
    }
}

TEST_CASE("Bitrate Crush knob scales distortion monotonically with knob amount", "[core][bitcrush]") {
    constexpr double kSr = 48000.0;
    constexpr int kN = 2048;

    auto measureDiffForAmount = [&](float amt) {
        GlitchProcessorCore core;
        core.prepare(kSr);
        core.applyPatternPreset(StepPatternPreset::AllOn);
        for (int i = 0; i < GlitchProcessorCore::kNumEffects; ++i) {
            core.setEffectAmount(static_cast<GlitchFxId>(i), 0.0f);
        }
        core.setEffectEnabled(GlitchFxId::Bitcrush, true);
        core.setEffectAmount(GlitchFxId::Bitcrush, amt);
        core.setTransport(120.0, true, true, 0.0);

        std::vector<float> dryL(kN), dryR(kN);
        fillSineStereo(dryL, dryR, kSr, 220.0);
        auto wetL = dryL;
        auto wetR = dryR;
        core.processBlock(wetL.data(), wetR.data(), kN);
        return computeRmsDifference(dryL, wetL);
    };

    const float diffZero = measureDiffForAmount(0.0f);
    const float diffLow  = measureDiffForAmount(0.25f);
    const float diffHigh = measureDiffForAmount(0.90f);

    REQUIRE(diffZero < 1e-5f);
    REQUIRE(diffLow > 0.01f);
    REQUIRE(diffHigh > diffLow);
}

TEST_CASE("Per-pad FX routing enables an effect on only a single 1/32 pad while other pads run different FX", "[core][per-pad]") {
    constexpr double kSr = 48000.0;
    constexpr int kN = 512;

    GlitchProcessorCore core;
    core.applyPatternPreset(StepPatternPreset::AllOn);
    // Assign ONLY Bitcrush (bit 0) to Pad 31 (the last 1/32 of the bar), and ONLY Wavefold (bit 9) to Pad 0
    core.setAllStepsFxMask(0u);
    core.setStepFxEnabled(0, GlitchFxId::Wavefold, true);
    core.setStepFxEnabled(31, GlitchFxId::Bitcrush, true);

    core.prepare(kSr);
    core.setGateSmoothingMs(0.0f);

    // Turn up Bitcrush to 90%, leave Wavefold at 0%
    core.setEffectEnabled(GlitchFxId::Bitcrush, true);
    core.setEffectAmount(GlitchFxId::Bitcrush, 0.90f);
    core.setEffectEnabled(GlitchFxId::Wavefold, true);
    core.setEffectAmount(GlitchFxId::Wavefold, 0.0f);

    std::vector<float> dryL(kN), dryR(kN);
    fillSineStereo(dryL, dryR, kSr, 440.0);

    // 1. At Pad 0 (PPQ = 0.0), Bitcrush is NOT assigned to Pad 0, so output must be completely dry
    auto pad0L = dryL;
    auto pad0R = dryR;
    core.setTransport(120.0, true, true, 0.0);
    core.processBlock(pad0L.data(), pad0R.data(), kN);
    REQUIRE(computeRmsDifference(dryL, pad0L) < 1e-5f);

    // 2. At Pad 31 (PPQ = 3.90), Bitcrush IS assigned to Pad 31, so Bitcrush engages
    auto pad31L = dryL;
    auto pad31R = dryR;
    core.setTransport(120.0, true, true, 3.90);
    core.processBlock(pad31L.data(), pad31R.data(), kN);
    REQUIRE(computeRmsDifference(dryL, pad31L) > 0.05f);
}

TEST_CASE("Linked adjacent 1/32 pads sustain effects continuously across step boundaries without resetting", "[core][sustain]") {
    constexpr double kSr = 48000.0;
    constexpr double kBpm = 120.0;
    // At 120 BPM, 1 quarter = 0.5s, 1/32 step = 0.0625s = 3000 samples at 48 kHz.
    // Two linked 1/32 steps (Steps 24 & 25) = 6000 samples (PPQ 3.0 to 3.25).
    constexpr int kTwoStepsSamples = 6000;

    GlitchProcessorCore core;
    core.applyPatternPreset(StepPatternPreset::ClearAll);
    core.setStep(24, true);
    core.setStep(25, true);
    core.prepare(kSr);
    core.setGateSmoothingMs(0.0f);

    for (int i = 0; i < GlitchProcessorCore::kNumEffects; ++i) {
        core.setEffectAmount(static_cast<GlitchFxId>(i), 0.0f);
    }
    // Enable TapeStop across the 2 linked steps
    core.setEffectEnabled(GlitchFxId::TapeStop, true);
    core.setEffectAmount(GlitchFxId::TapeStop, 0.80f);

    std::vector<float> left(kTwoStepsSamples), right(kTwoStepsSamples);
    fillSineStereo(left, right, kSr, 440.0, 0.8f);

    // Start right at Step 24 (PPQ = 3.0) and process across the Step 24 -> Step 25 boundary (sample 3000)
    core.setTransport(kBpm, true, true, 3.0);
    core.processBlock(left.data(), right.data(), kTwoStepsSamples);

    // Measure RMS in the second half of Step 24 (samples 1500..2900) vs first half of Step 25 (samples 3100..4500)
    // Because TapeStop decelerates continuously across the 2-step span without resetting at sample 3000,
    // the amplitude in Step 25 must be lower than in Step 24 (continuing the brake rather than restarting at 1.0x!).
    double sumSqStep24 = 0.0;
    double sumSqStep25 = 0.0;
    for (int i = 1500; i < 2900; ++i) {
        sumSqStep24 += static_cast<double>(left[static_cast<size_t>(i)] * left[static_cast<size_t>(i)]);
    }
    for (int i = 3100; i < 4500; ++i) {
        sumSqStep25 += static_cast<double>(left[static_cast<size_t>(i)] * left[static_cast<size_t>(i)]);
    }
    REQUIRE(sumSqStep25 < sumSqStep24);
}

TEST_CASE("ReverseGrainEffect plays recorded history when the input becomes silent", "[core][reverse]") {
    constexpr double kSr = 48000.0;
    constexpr int kN = 3000;

    GlitchProcessorCore core;
    core.applyPatternPreset(StepPatternPreset::Last32nd); // Only Step 31 enabled
    core.prepare(kSr);
    core.setGateSmoothingMs(0.0f);

    for (int i = 0; i < GlitchProcessorCore::kNumEffects; ++i) {
        core.setEffectAmount(static_cast<GlitchFxId>(i), 0.0f);
    }
    core.setEffectEnabled(GlitchFxId::Reverse, true);
    core.setEffectAmount(GlitchFxId::Reverse, 1.0f); // 100% wet reverse

    // 1. Pre-warm during inactive Step 30 (PPQ = 3.75) so dry audio is recorded into circular pre-roll buffer
    std::vector<float> warmL(kN), warmR(kN);
    fillSineStereo(warmL, warmR, kSr, 330.0, 0.8f);
    core.setTransport(120.0, true, true, 3.75);
    core.processBlock(warmL.data(), warmR.data(), kN);

    // 2. Enter active Step 31 (PPQ = 3.88) where input drops to silence, verifying Reverse plays back the pre-warmed audio!
    std::vector<float> revL(kN, 0.0f), revR(kN, 0.0f);
    core.setTransport(120.0, true, true, 3.88);
    core.processBlock(revL.data(), revR.data(), kN);

    double sumSq = 0.0;
    for (int i = 128; i < kN - 128; ++i) {
        sumSq += static_cast<double>(revL[static_cast<size_t>(i)] * revL[static_cast<size_t>(i)]);
    }
    const double rms = std::sqrt(sumSq / static_cast<double>(kN - 256));
    REQUIRE(rms > 0.25);
}

TEST_CASE("WavefolderEffect multi-stage West-Coast folder suppresses DC offset and generates harmonics", "[core][wavefold]") {
    constexpr double kSr = 48000.0;
    constexpr int kN = 8192;

    WavefolderEffect folder;
    folder.prepare(kSr);

    double meanL = 0.0;
    double energyL = 0.0;
    for (int i = 0; i < kN; ++i) {
        // Biased input sine wave (+0.25 DC bias)
        float l = 0.25f + 0.6f * std::sin(static_cast<float>(2.0 * std::numbers::pi * 220.0 * static_cast<double>(i) / kSr));
        float r = l;
        folder.processSample(0.85f, l, r);
        if (i >= 4096) {
            meanL += static_cast<double>(l);
            energyL += static_cast<double>(l * l);
        }
    }
    meanL /= 4096.0;
    const double rmsL = std::sqrt(energyL / 4096.0);

    REQUIRE(std::abs(meanL) < 0.05);
    REQUIRE(rmsL > 0.2);
}


namespace {

float computeRms(const std::vector<float>& x, size_t begin, size_t end) {
    double sumSq = 0.0;
    for (size_t i = begin; i < end; ++i) {
        sumSq += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(end - begin)));
}

void prepareAllOnCore(GlitchProcessorCore& core, double sampleRate) {
    core.prepare(sampleRate);
    core.setGateSmoothingMs(0.1f);
    core.applyPatternPreset(StepPatternPreset::AllOn);
    core.setTransport(120.0, true, true, 0.0);
}

} // namespace

TEST_CASE("Stutter gate time silences the tail of every synced repeat", "[core][stutter]") {
    constexpr double kSr = 48000.0;
    constexpr int kSlice = 6000; // 1/16 note at 120 BPM
    constexpr int kN = kSlice * 4;

    GlitchProcessorCore core;
    prepareAllOnCore(core, kSr);
    core.setEffectAmount(GlitchFxId::Stutter, 0.9f);
    StutterSettings settings;
    settings.syncedSliceBeats = 0.25;
    settings.gate = 0.5f;
    core.setStutterSettings(settings);

    std::vector<float> l(kN), r(kN);
    fillSineStereo(l, r, kSr, 440.0);
    core.processBlock(l.data(), r.data(), kN);

    const auto repeatStart = static_cast<size_t>(kSlice);
    REQUIRE(computeRms(l, repeatStart + 500, repeatStart + 2500) > 0.2f);
    REQUIRE(computeRms(l, repeatStart + 3500, repeatStart + 5900) < 1e-3f);
}

TEST_CASE("Stutter reverse plays the captured slice backwards", "[core][stutter]") {
    constexpr double kSr = 48000.0;
    constexpr int kSlice = 6000;
    constexpr int kN = kSlice * 2;

    GlitchProcessorCore core;
    prepareAllOnCore(core, kSr);
    core.setEffectAmount(GlitchFxId::Stutter, 0.9f);
    StutterSettings settings;
    settings.syncedSliceBeats = 0.25;
    settings.reverse = StutterReverse::All;
    core.setStutterSettings(settings);

    // A rising ramp shows the playback direction.
    std::vector<float> l(kN), r(kN);
    for (int i = 0; i < kN; ++i) {
        l[static_cast<size_t>(i)] = 0.8f * static_cast<float>(i % kSlice) / static_cast<float>(kSlice);
        r[static_cast<size_t>(i)] = l[static_cast<size_t>(i)];
    }
    core.processBlock(l.data(), r.data(), kN);

    REQUIRE(l[static_cast<size_t>(kSlice + 1000)] > l[static_cast<size_t>(kSlice + 4000)]);
}

TEST_CASE("Tape stop STOP fades out while START spins up", "[core][tapestop]") {
    constexpr double kSr = 48000.0;
    constexpr int kN = 24000; // one beat at 120 BPM

    auto runDirection = [&](TapeDirection direction) {
        GlitchProcessorCore core;
        prepareAllOnCore(core, kSr);
        core.setEffectAmount(GlitchFxId::TapeStop, 1.0f);
        TapeStopSettings settings;
        settings.direction = direction;
        core.setTapeStopSettings(settings, 1.0);

        std::vector<float> l(kN), r(kN);
        fillSineStereo(l, r, kSr, 220.0);
        core.processBlock(l.data(), r.data(), kN);
        return std::pair{ computeRms(l, 0, kN / 4), computeRms(l, 3 * kN / 4, kN) };
    };

    const auto [stopHead, stopTail] = runDirection(TapeDirection::Stop);
    const auto [startHead, startTail] = runDirection(TapeDirection::Start);
    REQUIRE(stopTail < stopHead);
    REQUIRE(startTail > startHead);
}

TEST_CASE("Wet low-pass filter attenuates highs on active pads", "[core][filter]") {
    constexpr double kSr = 48000.0;
    constexpr int kN = 8192;

    GlitchProcessorCore core;
    prepareAllOnCore(core, kSr);
    core.setFilter(FilterMode::LowPass, 300.0f, 0.0f);

    std::vector<float> l(kN), r(kN);
    fillSineStereo(l, r, kSr, 8000.0);
    const float inRms = computeRms(l, kN / 2, kN);
    core.processBlock(l.data(), r.data(), kN);

    REQUIRE(computeRms(l, kN / 2, kN) < inRms * 0.05f);
}

TEST_CASE("Volume and pan envelopes shape the wet signal", "[core][envelope]") {
    constexpr double kSr = 48000.0;
    constexpr int kN = 4096;

    GlitchProcessorCore core;
    prepareAllOnCore(core, kSr);
    core.setEnvelopeSettings(EnvTarget::Pan, { true, 1.0f, 0.0 });
    core.setEnvelopePoints(EnvTarget::Pan, { { 0.0f, 0.0f }, { 1.0f, 0.0f } }); // hard left

    std::vector<float> l(kN), r(kN);
    fillSineStereo(l, r, kSr, 440.0);
    const float inLeft = computeRms(l, 512, kN);
    core.processBlock(l.data(), r.data(), kN);
    REQUIRE_THAT(computeRms(l, 512, kN), Catch::Matchers::WithinRel(inLeft, 0.01f));
    REQUIRE(computeRms(r, 512, kN) < 1e-4f);

    core.setEnvelopeSettings(EnvTarget::Volume, { true, 1.0f, 0.0 });
    core.setEnvelopePoints(EnvTarget::Volume, { { 0.0f, 0.0f }, { 1.0f, 0.0f } });
    fillSineStereo(l, r, kSr, 440.0);
    core.processBlock(l.data(), r.data(), kN);
    REQUIRE(computeRms(l, 512, kN) < 1e-4f);
}

TEST_CASE("ModEnvelope interpolates linearly between drawn points", "[core][envelope]") {
    ModEnvelope env;
    env.setPoints({ { 1.0f, 1.0f }, { 0.0f, 0.0f } });
    REQUIRE_THAT(env.valueAt(0.5f), Catch::Matchers::WithinAbs(0.5f, 0.01f));
    REQUIRE_THAT(env.valueAt(0.0f), Catch::Matchers::WithinAbs(0.0f, 1e-4f));
    REQUIRE_THAT(env.valueAt(1.0f), Catch::Matchers::WithinAbs(1.0f, 1e-4f));
}

TEST_CASE("MIDI trigger mode ignores the pattern and glitches only while a note is held", "[core][midi]") {
    constexpr double kSr = 48000.0;
    constexpr int kN = 2048;

    GlitchProcessorCore core;
    prepareAllOnCore(core, kSr);
    core.setEffectAmount(GlitchFxId::Bitcrush, 0.9f);
    core.setTriggerMode(TriggerMode::Midi, false);

    std::vector<float> dryL(kN), dryR(kN);
    fillSineStereo(dryL, dryR, kSr, 220.0);
    auto l = dryL;
    auto r = dryR;
    core.processBlock(l.data(), r.data(), kN); // settles the gate envelope from prepare()
    l = dryL;
    r = dryR;
    core.processBlock(l.data(), r.data(), kN);
    REQUIRE(computeRmsDifference(dryL, l) < 1e-5f);

    core.queueMidiNote(0, kMidiSoloBaseNote, true); // solo note for Bitcrush
    l = dryL;
    r = dryR;
    core.processBlock(l.data(), r.data(), kN);
    REQUIRE(computeRmsDifference(dryL, l) > 0.01f);
}

TEST_CASE("Pitch glitch fourth interval jumps up 19 semitones", "[core][pitch]") {
    constexpr double fs = 48000.0;
    PitchGlitchEffect effect;
    effect.prepare(fs);
    std::vector<float> output(48000);
    for (size_t i = 0; i < output.size(); ++i) {
        const auto x = 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 200.0 * static_cast<double>(i) / fs));
        effect.recordDrySample(x, x);
        float left = x;
        float right = x;
        effect.processSample(1.0f, left, right);
        output[i] = left;
    }

    // At full amount each interval holds 12000 samples, so the fourth (+19) plays from 36000 on.
    const auto levelAt = [&](double hz) {
        double re = 0.0;
        double im = 0.0;
        for (size_t i = 38000; i < 47000; ++i) {
            const double angle = 2.0 * std::numbers::pi * hz * static_cast<double>(i) / fs;
            re += output[i] * std::cos(angle);
            im += output[i] * std::sin(angle);
        }
        return std::hypot(re, im);
    };
    REQUIRE(levelAt(200.0 * 2.996614) > 4.0 * levelAt(200.0 * 1.259921));
}

TEST_CASE("Bitrate crusher quantizes a full-scale signal to 2^bits levels", "[core][bitcrush]") {
    BitrateCrusher crusher;
    crusher.prepare(48000.0);
    for (const float amount : {0.8f, 1.0f}) {
        const float stepsPerUnit = std::pow(2.0f, 16.0f - amount * 14.5f - 1.0f);
        for (float input = 0.05f; input < 0.95f; input += 0.05f) {
            crusher.reset();
            float left = input;
            float right = input;
            for (int i = 0; i < 2000; ++i) {
                left = input;
                right = input;
                crusher.processSample(amount, left, right);
            }
            const float levels = left * stepsPerUnit;
            REQUIRE_THAT(levels, Catch::Matchers::WithinAbs(std::round(levels), 1e-3));
        }
    }
}

TEST_CASE("Wavefolder antiderivative anti-aliasing keeps folded harmonics from aliasing", "[core][wavefold]") {
    constexpr double fs = 48000.0;
    WavefolderEffect folder;
    folder.prepare(fs);
    std::vector<float> output(9600);
    for (size_t i = 0; i < output.size(); ++i) {
        float left = 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 7000.0 * static_cast<double>(i) / fs));
        float right = left;
        folder.processSample(0.5f, left, right);
        output[i] = left;
    }

    // Harmonics of 7 kHz sit at 7, 14 and 21 kHz. Folded-back aliases land on the other whole-kHz bins.
    const auto power = [&](double hz) {
        double re = 0.0;
        double im = 0.0;
        for (size_t i = 4800; i < output.size(); ++i) {
            const double angle = 2.0 * std::numbers::pi * hz * static_cast<double>(i) / fs;
            re += output[i] * std::cos(angle);
            im += output[i] * std::sin(angle);
        }
        return re * re + im * im;
    };
    double harmonics = 0.0;
    double aliases = 0.0;
    for (int khz = 1; khz < 24; ++khz) {
        (khz % 7 == 0 ? harmonics : aliases) += power(khz * 1000.0);
    }
    REQUIRE(10.0 * std::log10(aliases / harmonics) < -30.0);
}

TEST_CASE("Every FX keeps its audio continuous across mixed routing and the bar line", "[core][span]") {
    for (int fx = 0; fx < GlitchProcessorCore::kNumEffects; ++fx) {
        INFO("FX slot " << fx);
        auto render = [&](int start, bool mixed, int blockSize) {
            GlitchProcessorCore core;
            core.setAllStepsFxMask(0);
            for (int i = 0; i < 8; ++i) {
                const auto mask = static_cast<uint16_t>((1u << fx) | (mixed && i % 2 ? 1u << ((fx + 1) % 15) : 0));
                core.setStepFxMask((start + i) % 32, mask);
            }
            core.prepare(8000);
            core.setGateSmoothingMs(0);
            core.setEffectAmount(static_cast<GlitchFxId>(fx), 0.9f);
            std::vector<float> l(4000), r(4000);
            fillSineStereo(l, r, 8000, 113);
            core.setTransport(120, true, true, start * 0.125 - 1);
            core.processBlock(l.data(), r.data(), 4000);
            fillSineStereo(l, r, 8000, 219);
            for (int offset = 0; offset < 4000; offset += blockSize) {
                core.setTransport(120, true, true, start * 0.125 + offset / 4000.0);
                core.processBlock(l.data() + offset, r.data() + offset, std::min(blockSize, 4000 - offset));
            }
            l.insert(l.end(), r.begin(), r.end());
            return l;
        };
        const auto reference = render(8, false, 4000);
        REQUIRE(computeRmsDifference(reference, render(8, true, 127)) < 1e-5f);
        REQUIRE(computeRmsDifference(reference, render(28, true, 193)) < 1e-5f);
    }
}

TEST_CASE("Tape braking lasts the entire slow-tempo pad span", "[core][tapestop]") {
    TapeStopEffect tape;
    tape.prepare(8000);
    tape.triggerBrake();
    TapeStopSettings settings;
    settings.lowpass = false;
    float l = 0, r = 0;
    for (int i = 0; i <= 48000; ++i) {
        l = r = 1;
        tape.processSample(1, 12, settings, l, r);
    }
    REQUIRE_THAT(l, Catch::Matchers::WithinAbs(std::pow(0.5098, 0.36), 1e-4));
}

TEST_CASE("Stutter honors a quarter-note capture at 20 BPM", "[core][stutter]") {
    BufferStutter stutter;
    stutter.prepare(8000);
    StutterSettings settings;
    settings.syncedSliceBeats = 1;
    for (int i = 0; i < 24000; ++i) {
        const float dry = 0.2f + i / 48000.0f;
        float l = dry, r = dry;
        stutter.processSample(1, 20, settings, l, r);
        REQUIRE(l == dry);
    }
    float l = 0, r = 0;
    stutter.processSample(1, 20, settings, l, r);
    REQUIRE(l > 0);
}

TEST_CASE("Ping-pong echoes alternate sides for a centered impulse", "[core][echo]") {
    PingPongEchoEffect echo;
    echo.prepare(8000);
    std::vector<float> l(6100), r(6100);
    l[0] = r[0] = 1;
    for (size_t i = 0; i < l.size(); ++i) echo.processSample(1, 120, l[i], r[i]);
    REQUIRE(l[2000] > 0.5f);
    REQUIRE(std::abs(r[2000]) < 1e-6f);
    REQUIRE(r[4000] > 0.1f);
    REQUIRE(std::abs(l[4000]) < 1e-6f);
    REQUIRE(l[6000] > 0.05f);
}

TEST_CASE("Echo delay time keeps advancing through disabled pads", "[core][echo]") {
    GlitchProcessorCore core;
    core.setAllStepsFxMask(0);
    core.setStepFxMask(0, 1u << static_cast<int>(GlitchFxId::PingPong));
    core.setStepFxMask(4, 1u << static_cast<int>(GlitchFxId::PingPong));
    core.prepare(8000);
    core.setGateSmoothingMs(0);
    core.setEffectAmount(GlitchFxId::PingPong, 1);
    core.setTransport(120, true, true, 0);
    std::vector<float> l(2500), r(2500);
    l[0] = r[0] = 1;
    core.processBlock(l.data(), r.data(), 2500);
    REQUIRE(l[2000] > 0.5f);
    for (int i = 500; i < 2000; ++i) REQUIRE(l[i] == 0.0f);
}

TEST_CASE("Cold reverse fades from dry into the first available long reversal", "[core][reverse]") {
    GlitchProcessorCore core;
    core.prepare(8000);
    core.setEffectAmount(GlitchFxId::Reverse, 1);
    core.setTransport(120, true, true, 0);
    std::vector<float> l(16100, 0.5f), r = l;
    core.processBlock(l.data(), r.data(), static_cast<int>(l.size()));
    for (int i = 15980; i < 16080; ++i) REQUIRE(std::abs(l[i] - l[i - 1]) < 0.05f);
}

TEST_CASE("Extending an active reverse region does not restart at its old length", "[core][reverse]") {
    ReverseGrainEffect reverse;
    reverse.prepare(8000);
    for (int i = 0; i < 8000; ++i) reverse.recordDrySample(0.2f + 0.6f * i / 8000, 0);
    reverse.triggerGrain();
    for (int i = 0; i < 8000; ++i) {
        const double duration = i < 2000 ? 0.5 : 1.0;
        float l = 0, r = 0;
        reverse.processSample(1, static_cast<float>(i / (duration * 8000)), duration, l, r);
        reverse.recordDrySample(0, 0);
        if (i > 3000 && i < 7000) REQUIRE_THAT(l, Catch::Matchers::WithinAbs(0.2 + 0.6 * (7999 - i) / 8000, 1e-5));
    }
}

TEST_CASE("Wavefolder retains the same bass response at different sample rates", "[core][wavefold]") {
    double reference = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
        WavefolderEffect folder;
        folder.prepare(sr);
        double energy = 0;
        for (int i = 0; i < static_cast<int>(sr); ++i) {
            float l = 0.01f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 50.0 * i / sr));
            float r = l;
            folder.processSample(1, l, r);
            if (i >= static_cast<int>(sr / 2)) energy += l * l;
        }
        const double rms = std::sqrt(energy / (sr / 2));
        if (reference == 0) reference = rms;
        REQUIRE_THAT(rms, Catch::Matchers::WithinRel(reference, 0.03));
    }
}

TEST_CASE("Saturation suppresses folded aliases compared with direct tanh clipping", "[core][saturation]") {
    SaturationEffect saturation;
    saturation.prepare(48000);
    std::vector<float> processed(9600), naive(9600);
    for (size_t i = 0; i < processed.size(); ++i) {
        const float input = 0.8f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 7000 * i / 48000));
        float l = input, r = input;
        saturation.processSample(1, l, r);
        processed[i] = l;
        naive[i] = std::tanh(6 * input) / std::sqrt(6.0f);
    }
    auto aliasEnergy = [](const std::vector<float>& signal) {
        double energy = 0;
        for (int khz = 1; khz < 24; ++khz) {
            if (khz % 7 == 0) continue;
            double real = 0, imaginary = 0;
            for (size_t i = 4800; i < signal.size(); ++i) {
                const double angle = 2.0 * std::numbers::pi * khz * 1000 * i / 48000;
                real += signal[i] * std::cos(angle);
                imaginary += signal[i] * std::sin(angle);
            }
            energy += real * real + imaginary * imaginary;
        }
        return energy;
    };
    REQUIRE(aliasEnergy(processed) < aliasEnergy(naive) * 0.5);
}

TEST_CASE("Long synced stutters still alternate reverse on successive repeats", "[core][stutter]") {
    BufferStutter stutter;
    stutter.prepare(8000);
    StutterSettings settings;
    settings.syncedSliceBeats = 1;
    settings.reverse = StutterReverse::Alternate;
    float firstEarly = 0, firstLate = 0, secondEarly = 0, secondLate = 0;
    for (int i = 0; i < 72000; ++i) {
        float l = 0.2f + 0.6f * (i % 24000) / 24000, r = l;
        stutter.processSample(1, 20, settings, l, r);
        if (i == 30000) firstEarly = l;
        if (i == 42000) firstLate = l;
        if (i == 54000) secondEarly = l;
        if (i == 66000) secondLate = l;
    }
    REQUIRE(firstLate > firstEarly + 0.2f);
    REQUIRE(secondEarly > secondLate + 0.2f);
}

TEST_CASE("A faster tempo does not retrigger reverse in the middle of an active run", "[core][reverse]") {
    ReverseGrainEffect reverse;
    reverse.prepare(8000);
    for (int i = 0; i < 8000; ++i) reverse.recordDrySample(0.2f + 0.6f * i / 8000, 0);
    reverse.triggerGrain();
    for (int i = 0; i < 7000; ++i) {
        const double duration = i < 6000 ? 1.0 : 0.5;
        const float phase = i < 6000 ? i / 8000.0f : 0.75f + (i - 6000) / 4000.0f;
        float l = 0, r = 0;
        reverse.processSample(1, phase, duration, l, r);
        reverse.recordDrySample(0, 0);
        if (i > 5900) REQUIRE_THAT(l, Catch::Matchers::WithinAbs(0.2 + 0.6 * (7999 - i) / 8000, 1e-5));
    }
}

TEST_CASE("BufferStutter dual-head crossfade eliminates tremolo pumping at 1/64 loop wraps and retriggers", "[core][stutter]") {
    constexpr double kSr = 48000.0;
    BufferStutter stutter;
    stutter.prepare(kSr);
    StutterSettings settings;
    settings.syncedSliceBeats = 0.0625;
    settings.gate = 1.0f;

    // 1/64 at 120 BPM is 1500 samples; run across 4 repeats with constant unit-level input.
    float minWrapOutput = 1.0f;
    for (int i = 0; i < 6000; ++i) {
        float l = 0.8f;
        float r = 0.8f;
        stutter.processSample(1.0f, 120.0, settings, l, r);
        if (i >= 1500) {
            minWrapOutput = std::min(minWrapOutput, l);
        }
    }
    REQUIRE(minWrapOutput > 0.72f);

    // Retriggering mid-playback crossfades from the outgoing head without a single-sample jump.
    float prevL = 0.8f;
    float prevR = 0.8f;
    stutter.processSample(1.0f, 120.0, settings, prevL, prevR);
    stutter.triggerCapture();
    float maxStepJump = 0.0f;
    for (int i = 0; i < 512; ++i) {
        float l = -0.6f;
        float r = -0.6f;
        stutter.processSample(1.0f, 120.0, settings, l, r);
        maxStepJump = std::max(maxStepJump, std::abs(l - prevL));
        prevL = l;
    }
    REQUIRE(maxStepJump < 0.05f);
}

TEST_CASE("TapeStop circular varispeed buffer never exhausts after 15 seconds and sinc-filters high frequencies", "[core][tapestop]") {
    constexpr double kSr = 8000.0;
    TapeStopEffect tape;
    tape.prepare(kSr);
    TapeStopSettings settings;
    settings.fade = false;
    settings.lowpass = false;

    // Exceed the old 12-second linear buffer limit (15 seconds = 120000 samples), then retrigger on a new level.
    for (int i = 0; i < 120000; ++i) {
        float l = 0.1f;
        float r = 0.1f;
        tape.processSample(1.0f, 4.0, settings, l, r);
    }
    tape.triggerBrake();
    float afterRetriggerL = 0.0f;
    for (int i = 0; i < 2000; ++i) {
        float l = 0.75f;
        float r = 0.75f;
        tape.processSample(1.0f, 1.0, settings, l, r);
        afterRetriggerL = l;
    }
    REQUIRE_THAT(afterRetriggerL, Catch::Matchers::WithinAbs(0.75f, 0.02f));

    // Verify dynamic polyphase sinc lowpass strongly attenuates 16 kHz at 48 kHz as speed decelerates near 0.
    TapeStopEffect hfTape;
    hfTape.prepare(48000.0);
    hfTape.triggerBrake();
    TapeStopSettings lpSettings;
    lpSettings.fade = false;
    lpSettings.lowpass = true;
    double tailSq = 0.0;
    for (int i = 0; i < 24000; ++i) {
        float s = 0.8f * std::sin(static_cast<float>(2.0 * std::numbers::pi * 16000.0 * i / 48000.0));
        float l = s;
        float r = s;
        hfTape.processSample(1.0f, 0.25, lpSettings, l, r);
        if (i >= 18000) {
            tailSq += static_cast<double>(l * l);
        }
    }
    const double tailRms = std::sqrt(tailSq / 6000.0);
    REQUIRE(tailRms < 0.03);
}

TEST_CASE("ReverseGrainEffect dual-grain Hermite overlap eliminates zero-dip dropouts across fractional beat wraps", "[core][reverse]") {
    constexpr double kSr = 48000.0;
    const double fractionalSpanSec = 7.5 / 127.0;
    ReverseGrainEffect reverse;
    reverse.prepare(kSr);

    for (int i = 0; i < 8000; ++i) {
        reverse.recordDrySample(0.6f, 0.6f);
    }
    reverse.triggerGrain();

    const double spanSamples = fractionalSpanSec * kSr;
    float minAcrossWrap = 1.0f;
    for (int i = 0; i < 7000; ++i) {
        const float phase = static_cast<float>(std::fmod(static_cast<double>(i), spanSamples) / spanSamples);
        float l = 0.6f;
        float r = 0.6f;
        reverse.processSample(1.0f, phase, fractionalSpanSec, l, r);
        reverse.recordDrySample(0.6f, 0.6f);
        minAcrossWrap = std::min(minAcrossWrap, l);
    }
    REQUIRE(minAcrossWrap > 0.52f);
}

TEST_CASE("PitchGlitchEffect WSOLA keeps drum transients tight within 22 ms without 42 ms flam echoes", "[core][pitch]") {
    constexpr double kSr = 48000.0;
    PitchGlitchEffect pitch;
    pitch.prepare(kSr);

    // Pre-warm silence, then inject a crisp 2 ms percussive transient burst at sample 4000.
    std::vector<float> out(8000, 0.0f);
    for (int i = 0; i < static_cast<int>(out.size()); ++i) {
        const bool inBurst = (i >= 4000 && i < 4096);
        const float env = inBurst ? (1.0f - static_cast<float>(i - 4000) / 96.0f) : 0.0f;
        const float x = env * std::sin(static_cast<float>(2.0 * std::numbers::pi * 900.0 * i / kSr));
        pitch.recordDrySample(x, x);
        float l = x;
        float r = x;
        pitch.processSample(1.0f, l, r);
        out[static_cast<size_t>(i)] = l;
    }

    const float mainHitRms = computeRms(out, 4000, 5056);
    const float lateFlamRms = computeRms(out, 5350, 6400);
    REQUIRE(mainHitRms > 0.02f);
    REQUIRE(lateFlamRms < mainHitRms * 0.08f);
}

TEST_CASE("Keep Sub LR4 24 dB/oct crossover blocks uncorrelated wet sub bleed and sums flat", "[core][keepsub]") {
    constexpr double kSr = 48000.0;
    GlitchProcessorCore core;
    core.applyPatternPreset(StepPatternPreset::Last32nd);
    core.prepare(kSr);
    core.setGateSmoothingMs(0.0f);
    core.setKeepSubEnabled(true);
    for (int i = 0; i < GlitchProcessorCore::kNumEffects; ++i) {
        core.setEffectAmount(static_cast<GlitchFxId>(i), 0.0f);
    }
    core.setEffectEnabled(GlitchFxId::Reverse, true);
    core.setEffectAmount(GlitchFxId::Reverse, 1.0f);

    // Pre-warm Reverse buffer on inactive Step 30 with a loud 30 Hz sub-bass sine.
    std::vector<float> warmL(4000), warmR(4000);
    fillSineStereo(warmL, warmR, kSr, 30.0, 0.9f);
    core.setTransport(120.0, true, true, 3.75);
    core.processBlock(warmL.data(), warmR.data(), 4000);

    // Enter active Step 31 with silent dry input: Reverse tries to output 30 Hz wet sub, which LR4 HPF must suppress > 28 dB.
    std::vector<float> silentDryL(3000, 0.0f), silentDryR(3000, 0.0f);
    core.setTransport(120.0, true, true, 3.88);
    core.processBlock(silentDryL.data(), silentDryR.data(), 3000);
    const float leakedWetSubRms = computeRms(silentDryL, 800, 2800);
    REQUIRE(leakedWetSubRms < 0.03f);
}

TEST_CASE("WetFilter smooths steep resonant cutoff jumps per sample without crackle spikes", "[core][filter]") {
    constexpr double kSr = 48000.0;
    WetFilter filter;
    filter.prepare(kSr);

    float prevL = 0.0f;
    float maxSampleDelta = 0.0f;
    for (int i = 0; i < 2048; ++i) {
        const float cutoff = (i < 1024) ? 400.0f : 12000.0f;
        float l = 0.4f * std::sin(static_cast<float>(2.0 * std::numbers::pi * 400.0 * i / kSr));
        float r = l;
        filter.processSample(FilterMode::LowPass, cutoff, 0.98f, l, r);
        if (i >= 1020 && i <= 1040) {
            maxSampleDelta = std::max(maxSampleDelta, std::abs(l - prevL));
        }
        prevL = l;
    }
    REQUIRE(maxSampleDelta < 0.6f);
}

TEST_CASE("SampleRateDecimator PolyBLEP and oversampled BitrateCrusher suppress foldback aliasing", "[core][decimator][bitcrush]") {
    constexpr double kSr = 48000.0;
    constexpr int kN = 9600;
    SampleRateDecimator decimator;
    decimator.prepare(kSr);

    std::vector<float> decimated(kN), naiveZoh(kN);
    double zohPhase = 1.0;
    float zohHeld = 0.0f;
    const float amt = 0.36f;
    const double step = 1.0 / (1.0 + static_cast<double>(amt * amt) * 58.0);
    for (int i = 0; i < kN; ++i) {
        const float in = 0.7f * std::sin(static_cast<float>(2.0 * std::numbers::pi * 7000.0 * i / kSr));
        float l = in;
        float r = in;
        decimator.processSample(amt, l, r);
        decimated[static_cast<size_t>(i)] = l;

        zohPhase += step;
        if (zohPhase >= 1.0) {
            zohPhase -= 1.0;
            zohHeld = in;
        }
        const float wet = std::min(1.0f, amt * 1.35f);
        naiveZoh[static_cast<size_t>(i)] = in * (1.0f - wet) + zohHeld * wet;
    }

    auto highFreqAliasEnergy = [&](const std::vector<float>& x) {
        double energy = 0.0;
        for (int hz = 15500; hz <= 22500; hz += 500) {
            if (hz == 21000) continue;
            double re = 0.0;
            double im = 0.0;
            for (size_t i = 4800; i < x.size(); ++i) {
                const double a = 2.0 * std::numbers::pi * hz * static_cast<double>(i) / kSr;
                re += x[i] * std::cos(a);
                im += x[i] * std::sin(a);
            }
            energy += re * re + im * im;
        }
        return energy;
    };
    REQUIRE(highFreqAliasEnergy(decimated) < highFreqAliasEnergy(naiveZoh) * 0.35);
}

TEST_CASE("Multi-FX routing gain-stages stacked nonlinear effects and supports parallel time-vs-spectral summing", "[core][gainstaging]") {
    constexpr double kSr = 48000.0;
    constexpr int kN = 12000;
    GlitchProcessorCore core;
    prepareAllOnCore(core, kSr);

    for (auto id : { GlitchFxId::Bitcrush, GlitchFxId::Downsample, GlitchFxId::Wavefold,
                     GlitchFxId::RingMod, GlitchFxId::CombReso, GlitchFxId::Saturation }) {
        core.setEffectEnabled(id, true);
        core.setEffectAmount(id, 0.85f);
    }

    std::vector<float> l(kN), r(kN);
    fillSineStereo(l, r, kSr, 220.0, 0.125f);
    const float inputRms = computeRms(l, 2000, kN);
    core.processBlock(l.data(), r.data(), kN);
    const float stackedRms = computeRms(l, 2000, kN);
    REQUIRE(stackedRms < inputRms * 2.5f);
    REQUIRE(stackedRms > inputRms * 0.4f);

    // ParallelBuses mode sums time FX and spectral FX without series intermodulation blowup.
    GlitchProcessorCore parallelCore;
    prepareAllOnCore(parallelCore, kSr);
    parallelCore.setFxRoutingMode(FxRoutingMode::ParallelBuses);
    parallelCore.setEffectAmount(GlitchFxId::Stutter, 0.8f);
    parallelCore.setEffectAmount(GlitchFxId::Wavefold, 0.85f);

    GlitchProcessorCore seriesCore;
    prepareAllOnCore(seriesCore, kSr);
    seriesCore.setFxRoutingMode(FxRoutingMode::Series);
    seriesCore.setEffectAmount(GlitchFxId::Stutter, 0.8f);
    seriesCore.setEffectAmount(GlitchFxId::Wavefold, 0.85f);

    std::vector<float> parL(kN), parR(kN), serL(kN), serR(kN);
    fillSineStereo(parL, parR, kSr, 220.0, 0.4f);
    serL = parL;
    serR = parR;
    parallelCore.processBlock(parL.data(), parR.data(), kN);
    seriesCore.processBlock(serL.data(), serR.data(), kN);
    REQUIRE(computeRmsDifference(parL, serL) > 0.01f);
}

TEST_CASE("PingPong and CombReso use post-fader send gating so wet tails ring across disabled steps", "[core][sendgate]") {
    constexpr double kSr = 8000.0;
    GlitchProcessorCore core;
    core.setAllStepsFxMask(0u);
    // Enable PingPong ONLY on Step 0 (samples 0..499); Step 4 (sample 2000) is disabled!
    core.setStepFxMask(0, static_cast<uint16_t>(1u << static_cast<int>(GlitchFxId::PingPong)));
    core.prepare(kSr);
    core.setGateSmoothingMs(0.0f);
    for (int i = 0; i < GlitchProcessorCore::kNumEffects; ++i) {
        core.setEffectAmount(static_cast<GlitchFxId>(i), 0.0f);
    }
    core.setEffectEnabled(GlitchFxId::PingPong, true);
    core.setEffectAmount(GlitchFxId::PingPong, 1.0f);
    core.setTransport(120.0, true, true, 0.0);

    std::vector<float> l(2500, 0.0f), r(2500, 0.0f);
    l[0] = 1.0f;
    r[0] = 1.0f;
    core.processBlock(l.data(), r.data(), 2500);
    REQUIRE(l[2000] > 0.4f);

    // An impulse arriving on disabled Step 1 (sample 600) must NOT enter the echo send.
    std::fill(l.begin(), l.end(), 0.0f);
    std::fill(r.begin(), r.end(), 0.0f);
    core.reset();
    core.setTransport(120.0, true, true, 0.0);
    l[600] = 1.0f;
    r[600] = 1.0f;
    core.processBlock(l.data(), r.data(), 2500);
    REQUIRE(std::abs(l[600] - 1.0f) < 1e-5f);
    std::vector<float> tailL(600, 0.0f), tailR(600, 0.0f);
    core.setTransport(120.0, true, true, 2500.0 / 4000.0);
    core.processBlock(tailL.data(), tailR.data(), 600);
    REQUIRE(std::abs(tailL[100]) < 1e-5f);
}

TEST_CASE("PingPongEcho SVF band-limits repeats to 120 Hz-7 kHz and morphs tempo changes smoothly", "[core][echo]") {
    constexpr double kSr = 48000.0;
    auto measureEchoBandGain = [&](double freqHz) {
        PingPongEchoEffect echo;
        echo.prepare(kSr);
        constexpr int kDelay = 12000;
        std::vector<float> l(kDelay * 2 + 4500, 0.0f), r(kDelay * 2 + 4500, 0.0f);
        for (int i = 0; i < 4000; ++i) {
            const float s = 0.5f * std::sin(static_cast<float>(2.0 * std::numbers::pi * freqHz * i / kSr));
            l[static_cast<size_t>(i)] = s;
            r[static_cast<size_t>(i)] = s;
        }
        for (size_t i = 0; i < l.size(); ++i) {
            echo.processSample(1.0f, 120.0, l[i], r[i]);
        }
        return computeRms(r, kDelay * 2 + 500, kDelay * 2 + 3500);
    };

    const float midGain = measureEchoBandGain(1000.0);
    const float subGain = measureEchoBandGain(40.0);
    const float highGain = measureEchoBandGain(16000.0);
    REQUIRE(subGain < midGain * 0.35f);
    REQUIRE(highGain < midGain * 0.30f);

    // Tempo jump mid-echo uses equal-power tap morphing instead of Doppler pitch slewing.
    PingPongEchoEffect morphEcho;
    morphEcho.prepare(kSr);
    float prev = 0.0f;
    float maxDiff = 0.0f;
    for (int i = 0; i < 36000; ++i) {
        const double bpm = (i < 18000) ? 120.0 : 88.0;
        float l = 0.4f * std::sin(static_cast<float>(2.0 * std::numbers::pi * 220.0 * i / kSr));
        float r = l;
        morphEcho.processSample(1.0f, bpm, l, r);
        if (i > 17900) {
            maxDiff = std::max(maxDiff, std::abs(l - prev));
        }
        prev = l;
    }
    REQUIRE(maxDiff < 0.03f);
}

TEST_CASE("Phaser 12-stage ZDF cascade and Thiran Flanger create multi-notch dispersion without click spikes", "[core][modulation]") {
    constexpr double kSr = 48000.0;
    PhaserEffect phaser;
    phaser.prepare(kSr);

    // Measure steady-state phase shift / notch count across frequency on a slow LFO snapshot or chirp.
    // With 12 ZDF allpass stages, total phase rotation spans up to 12 * pi (6 full cycles -> up to 6 notches),
    // whereas a 6-stage phaser can produce at most 3 notches!
    int signFlips = 0;
    int prevSign = 0;
    for (int m = 0; m < 40; ++m) {
        const double hz = 120.0 * std::pow(12000.0 / 120.0, m / 39.0);
        PhaserEffect probe;
        probe.prepare(kSr);
        double dot = 0.0;
        for (int i = 0; i < 600; ++i) {
            const float in = 0.05f * std::sin(static_cast<float>(2.0 * std::numbers::pi * hz * i / kSr));
            float l = in;
            float r = in;
            probe.processSample(0.95f, l, r);
            if (i >= 300) {
                const float wetEst = (l - in * (1.0f - 0.5f * 0.95f)) / (0.5f * 0.95f);
                dot += static_cast<double>(in * wetEst);
            }
        }
        const int sign = (dot >= 0.0) ? 1 : -1;
        if (prevSign != 0 && sign != prevSign) {
            ++signFlips;
        }
        prevSign = sign;
    }
    REQUIRE(signFlips >= 8);

    // Verify Thiran Flanger modulates across integer sample boundaries smoothly without click spikes.
    ModulatedDelayEffect flanger;
    flanger.prepare(kSr);
    float prevL = 0.0f;
    float maxSecondDiff = 0.0f;
    float prevDiff = 0.0f;
    for (int i = 0; i < 12000; ++i) {
        float l = 0.5f * std::sin(static_cast<float>(2.0 * std::numbers::pi * 330.0 * i / kSr));
        float r = l;
        flanger.processSample(ModulatedDelayEffect::Mode::Flanger, 0.9f, l, r);
        const float diff = l - prevL;
        if (i > 200) {
            maxSecondDiff = std::max(maxSecondDiff, std::abs(diff - prevDiff));
        }
        prevDiff = diff;
        prevL = l;
    }
    REQUIRE(maxSecondDiff < 0.08f);
}
