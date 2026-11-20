#include "../Source/Core/BeatStepSequencer.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace openglitch::core;

TEST_CASE("Every FX triggers once per run even when neighboring routing changes", "[sequencer][span]") {
    for (int fx = 0; fx < 15; ++fx) {
        INFO("FX slot " << fx);
        BeatStepSequencer seq;
        seq.setAllStepsFxMask(0);
        const auto bit = static_cast<uint16_t>(1u << fx);
        seq.setStepFxMask(0, bit);
        seq.setStepFxMask(1, BeatStepSequencer::kAllFxMask);
        seq.setStepFxMask(2, bit);
        seq.setStepFxMask(4, bit);
        seq.prepare(48000);
        bool entered = false;
        uint16_t transitions = 0;
        seq.advanceSampleWithFxMask(120, true, true, 0.0, entered, transitions);
        REQUIRE((transitions & bit) != 0);
        for (double ppq : { 0.125, 0.25 }) {
            seq.advanceSampleWithFxMask(120, true, true, ppq, entered, transitions);
            REQUIRE((transitions & bit) == 0);
            REQUIRE(seq.getActiveFxSpan(seq.getCurrentStep(), fx).lengthSteps == 3);
            REQUIRE(seq.getFxEnvelope(fx) == 1.0f);
        }
        seq.advanceSampleWithFxMask(120, true, true, 0.375, entered, transitions);
        seq.advanceSampleWithFxMask(120, true, true, 0.5, entered, transitions);
        REQUIRE((transitions & bit) != 0);
    }
}

TEST_CASE("An FX span crossing the bar has one duration and continuous phase", "[sequencer][span]") {
    BeatStepSequencer seq;
    seq.setAllSteps(false);
    for (int step : { 30, 31, 0, 1 }) seq.setStep(step, true);
    seq.prepare(48000);
    bool entered = false;
    uint16_t transitions = 0;
    const std::array<double, 4> ppq = { 3.75, 3.875, 4.0, 4.125 };
    for (size_t i = 0; i < ppq.size(); ++i) {
        seq.advanceSampleWithFxMask(120, true, true, ppq[i], entered, transitions);
        const auto span = seq.getActiveFxSpan(seq.getCurrentStep(), 5);
        REQUIRE_THAT(seq.getMergedPadSpan(seq.getCurrentStep()).spanPhase, Catch::Matchers::WithinAbs(0.25 * i, 1e-6));
        REQUIRE(span.startStep == 30);
        REQUIRE(span.endStep == 1);
        REQUIRE(span.lengthSteps == 4);
        REQUIRE_THAT(span.spanPhase, Catch::Matchers::WithinAbs(0.25 * i, 1e-6));
        REQUIRE(transitions == (i == 0 ? BeatStepSequencer::kAllFxMask : 0));
    }
}

TEST_CASE("BeatStepSequencer maps 4/4 bar PPQ to 32 steps accurately", "[sequencer]") {
    double phase = 0.0;

    REQUIRE(BeatStepSequencer::computeStepFromPpq(0.0, &phase) == 0);
    REQUIRE_THAT(phase, Catch::Matchers::WithinAbs(0.0, 1e-6));

    REQUIRE(BeatStepSequencer::computeStepFromPpq(1.0, &phase) == 8);
    REQUIRE_THAT(phase, Catch::Matchers::WithinAbs(0.25, 1e-6));

    REQUIRE(BeatStepSequencer::computeStepFromPpq(2.0, &phase) == 16);
    REQUIRE_THAT(phase, Catch::Matchers::WithinAbs(0.5, 1e-6));

    REQUIRE(BeatStepSequencer::computeStepFromPpq(3.0, &phase) == 24);
    REQUIRE_THAT(phase, Catch::Matchers::WithinAbs(0.75, 1e-6));

    REQUIRE(BeatStepSequencer::computeStepFromPpq(3.88, &phase) == 31);
    REQUIRE(BeatStepSequencer::computeStepFromPpq(4.0, &phase) == 0);
}

TEST_CASE("BeatStepSequencer loops over the selected dynamic length", "[sequencer][timing]") {
    BeatStepSequencer seq;
    seq.setSequenceLength(16);
    seq.prepare(48000.0);
    seq.setSmoothingTimeMs(0.0f);
    bool entered = false;

    seq.advanceSample(120.0, true, true, 1.0, entered);
    REQUIRE(seq.getCurrentStep() == 8);
    seq.advanceSample(120.0, true, true, 2.0, entered);
    REQUIRE(seq.getCurrentStep() == 0);

    seq.setSequenceLength(32);
    seq.advanceSample(120.0, true, true, 2.0, entered);
    REQUIRE(seq.getCurrentStep() == 16);
}

TEST_CASE("BeatStepSequencer swing delays offbeat steps and leaves downbeats in place", "[sequencer][timing]") {
    BeatStepSequencer seq;
    seq.setSwing(100.0f);
    seq.prepare(48000.0);
    seq.setSmoothingTimeMs(0.0f);
    bool entered = false;

    seq.advanceSample(120.0, true, true, 0.0, entered);
    REQUIRE(seq.getCurrentStep() == 0);
    seq.advanceSample(120.0, true, true, 0.18, entered);
    REQUIRE(seq.getCurrentStep() == 0);
    seq.advanceSample(120.0, true, true, 0.19, entered);
    REQUIRE(seq.getCurrentStep() == 1);
    seq.advanceSample(120.0, true, true, 0.25, entered);
    REQUIRE(seq.getCurrentStep() == 2);
}

TEST_CASE("BeatStepSequencer spans wrap at the selected dynamic length", "[sequencer][span]") {
    BeatStepSequencer seq;
    seq.setSequenceLength(16);
    seq.setAllSteps(false);
    for (int step : { 14, 15, 0, 1 }) seq.setStep(step, true);
    seq.prepare(48000.0);
    bool entered = false;
    seq.advanceSample(120.0, true, true, 0.0, entered);

    const auto span = seq.getMergedPadSpan(0);
    REQUIRE(span.startStep == 14);
    REQUIRE(span.endStep == 1);
    REQUIRE(span.lengthSteps == 4);
    REQUIRE_THAT(span.spanPhase, Catch::Matchers::WithinAbs(0.5f, 1e-6));
}

TEST_CASE("BeatStepSequencer presets configure expected 32-step patterns", "[sequencer]") {
    BeatStepSequencer seq;

    seq.applyPreset(StepPatternPreset::LastBeat);
    for (int i = 0; i < 32; ++i) {
        REQUIRE(seq.getStep(i) == (i >= 24));
    }

    seq.applyPreset(StepPatternPreset::Last32nd);
    for (int i = 0; i < 32; ++i) {
        REQUIRE(seq.getStep(i) == (i == 31));
    }

    seq.applyPreset(StepPatternPreset::LastFourSteps);
    for (int i = 0; i < 32; ++i) {
        REQUIRE(seq.getStep(i) == (i >= 28));
    }

    seq.applyPreset(StepPatternPreset::ClearAll);
    REQUIRE(seq.getPatternBitmask() == 0u);

    seq.applyPreset(StepPatternPreset::AllOn);
    REQUIRE(seq.getPatternBitmask() == 0xFFFFFFFFu);
}

TEST_CASE("BeatStepSequencer advances gate and detects active transitions", "[sequencer]") {
    BeatStepSequencer seq;
    seq.applyPreset(StepPatternPreset::Last32nd); // Only step 31 enabled
    seq.prepare(48000.0);
    seq.setSmoothingTimeMs(0.1f);

    bool enteredActive = false;
    float envInactive = seq.advanceSample(120.0, true, true, 0.5, enteredActive);
    REQUIRE_FALSE(enteredActive);
    REQUIRE(seq.getCurrentStep() == 4);
    REQUIRE(envInactive < 0.05f);

    // Jump into step 31 (PPQ = 3.90)
    float envActive = seq.advanceSample(120.0, true, true, 3.90, enteredActive);
    REQUIRE(enteredActive);
    REQUIRE(seq.getCurrentStep() == 31);
    REQUIRE(envActive > 0.1f);
}

TEST_CASE("BeatStepSequencer tracks independent per-pad 10-FX routing masks and envelopes", "[sequencer][per-pad]") {
    BeatStepSequencer seq;
    seq.applyPreset(StepPatternPreset::AllOn);
    // Step 0: only FX 0 (Bitcrush) enabled
    seq.setStepFxMask(0, static_cast<uint16_t>(1u << 0));
    // Step 1: only FX 3 (TapeStop) enabled
    seq.setStepFxMask(1, static_cast<uint16_t>(1u << 3));

    seq.prepare(48000.0);
    seq.setSmoothingTimeMs(0.0f);

    bool stepEntered = false;
    uint16_t fxTransitionMask = 0u;

    seq.advanceSampleWithFxMask(120.0, true, true, 0.01, stepEntered, fxTransitionMask);
    REQUIRE(seq.getCurrentStep() == 0);
    REQUIRE(seq.getFxEnvelope(0) > 0.99f);
    REQUIRE(seq.getFxEnvelope(3) < 0.01f);

    seq.advanceSampleWithFxMask(120.0, true, true, 0.15, stepEntered, fxTransitionMask);
    REQUIRE(seq.getCurrentStep() == 1);
    REQUIRE((fxTransitionMask & (1u << 3)) != 0u);
    REQUIRE(seq.getFxEnvelope(0) < 0.01f);
    REQUIRE(seq.getFxEnvelope(3) > 0.99f);
}

TEST_CASE("Adjacent enabled pads combine into a contiguous span and do NOT reset between 1/32 steps", "[sequencer][span]") {
    BeatStepSequencer seq;
    seq.applyPreset(StepPatternPreset::LastBeat); // Steps 24..31 enabled (8 adjacent pads)
    seq.prepare(48000.0);
    seq.setSmoothingTimeMs(0.0f);

    const auto mergedSpan = seq.getMergedPadSpan(27);
    REQUIRE(mergedSpan.startStep == 24);
    REQUIRE(mergedSpan.endStep == 31);
    REQUIRE(mergedSpan.lengthSteps == 8);

    bool stepEntered = false;
    uint16_t fxTransitionMask = 0u;

    // 1. Before Beat 4 (Step 23, PPQ = 2.90) -> inactive
    seq.advanceSampleWithFxMask(120.0, true, true, 2.90, stepEntered, fxTransitionMask);
    REQUIRE_FALSE(stepEntered);
    REQUIRE(fxTransitionMask == 0u);

    // 2. Enter Pad 24 (First pad of Beat 4, PPQ = 3.01) -> triggers ONCE at start of combined block
    seq.advanceSampleWithFxMask(120.0, true, true, 3.01, stepEntered, fxTransitionMask);
    REQUIRE(stepEntered);
    REQUIRE(fxTransitionMask == BeatStepSequencer::kAllFxMask);

    // 3. Advance across adjacent pads 25, 26, 27, 28, 29, 30, 31 -> MUST NOT reset/retrigger!
    for (int s = 25; s <= 31; ++s) {
        const double ppq = (static_cast<double>(s) + 0.25) * (4.0 / 32.0);
        seq.advanceSampleWithFxMask(120.0, true, true, ppq, stepEntered, fxTransitionMask);
        REQUIRE(seq.getCurrentStep() == s);
        REQUIRE_FALSE(stepEntered);
        REQUIRE(fxTransitionMask == 0u);
        REQUIRE(seq.getCurrentEnvelope() > 0.99f);
    }
}

TEST_CASE("BeatStepSequencer uses asymmetric category micro-envelopes instead of one global 1-pole curve", "[sequencer][envelopes]") {
    BeatStepSequencer seq;
    seq.setAllSteps(false);
    seq.setStep(1, true);
    seq.setStepFxMask(1, BeatStepSequencer::kAllFxMask);
    seq.prepare(48000.0);
    seq.setSmoothingTimeMs(2.5f);

    bool entered = false;
    uint16_t transitions = 0u;
    seq.advanceSampleWithFxMask(120.0, true, true, 0.0, entered, transitions);

    // Sample 1 of attack on Step 1: Chopper (FX 6) uses a 3 ms raised-cosine edge (gentle initial slope)
    // while Bitcrush (FX 0) uses a fast punchy attack and slower release.
    seq.advanceSampleWithFxMask(120.0, true, true, 0.126, entered, transitions);
    const float firstChopperAttack = seq.getFxEnvelope(6);
    const float firstBitcrushAttack = seq.getFxEnvelope(0);
    REQUIRE(firstChopperAttack < 0.01f);
    REQUIRE(firstBitcrushAttack > firstChopperAttack);

    // Advance 40 samples (~0.83 ms): Buffer splice Stutter (FX 2, ~1.75 ms S-curve) rises faster than 3 ms Chopper.
    for (int n = 0; n < 40; ++n) {
        seq.advanceSampleWithFxMask(120.0, true, true, 0.126 + (n + 1) * 1e-5, entered, transitions);
    }
    const float midStutterAttack = seq.getFxEnvelope(2);
    const float midChopperAttack = seq.getFxEnvelope(6);
    REQUIRE(midStutterAttack > midChopperAttack + 0.1f);

    // Settle all envelopes at 1.0, then step to inactive Step 2 and measure asymmetric release after 96 samples (2 ms).
    for (int n = 0; n < 400; ++n) {
        seq.advanceSampleWithFxMask(120.0, true, true, 0.14 + n * 1e-5, entered, transitions);
    }
    REQUIRE(seq.getFxEnvelope(0) > 0.999f);
    for (int n = 0; n < 96; ++n) {
        seq.advanceSampleWithFxMask(120.0, true, true, 0.26 + n * 1e-5, entered, transitions);
    }
    const float bitcrushReleaseAt2ms = seq.getFxEnvelope(0);
    // Attack reached > 0.95 within 2 ms, whereas asymmetric release (~4 ms) still retains substantial level at 2 ms.
    REQUIRE(bitcrushReleaseAt2ms > 0.25f);
}
