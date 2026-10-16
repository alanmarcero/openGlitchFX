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
