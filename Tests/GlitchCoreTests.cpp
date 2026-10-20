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

TEST_CASE("All 10 Glitch FX (Phase 1 + Phase 2) modify signal when beat step is enabled", "[core][fx]") {
    constexpr double kSr = 48000.0;
    constexpr int kN = 4096;

    const std::array<GlitchFxId, 10> allFx = {
        GlitchFxId::Bitcrush,
        GlitchFxId::Downsample,
        GlitchFxId::Stutter,
        GlitchFxId::TapeStop,
        GlitchFxId::RingMod,
        GlitchFxId::Reverse,
        GlitchFxId::Chopper,
        GlitchFxId::PitchGlitch,
        GlitchFxId::CombReso,
        GlitchFxId::Wavefold
    };

    for (auto fx : allFx) {
        GlitchProcessorCore core;
        core.prepare(kSr);
        core.applyPatternPreset(StepPatternPreset::AllOn);

        for (int i = 0; i < GlitchProcessorCore::kNumEffects; ++i) {
            core.setEffectAmount(static_cast<GlitchFxId>(i), 0.0f);
        }
        core.setEffectEnabled(fx, true);
        core.setEffectAmount(fx, 0.85f);
        core.setTransport(120.0, true, true, 3.1); // Beat 4 active

        std::vector<float> dryL(kN), dryR(kN);
        fillSineStereo(dryL, dryR, kSr, 330.0);
        auto wetL = dryL;
        auto wetR = dryR;

        core.processBlock(wetL.data(), wetR.data(), kN);

        const float diff = computeRmsDifference(dryL, wetL);
        INFO("Testing FX ID: " << static_cast<int>(fx));
        REQUIRE(diff > 0.01f);
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

TEST_CASE("ReverseGrainEffect outputs full reversed waveform energy on both cold start and pre-warmed history", "[core][reverse]") {
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
