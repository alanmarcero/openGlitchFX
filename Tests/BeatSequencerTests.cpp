#include "../Source/Core/BeatStepSequencer.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace openglitch::core;

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
    seq.setSmoothingTimeMs(0.0f); // Instant switching for exact verification

    bool stepEntered = false;
    uint16_t fxTransitionMask = 0u;

    // Advance at PPQ = 0.01 (Step 0)
    seq.advanceSampleWithFxMask(120.0, true, true, 0.01, stepEntered, fxTransitionMask);
    REQUIRE(seq.getCurrentStep() == 0);
    REQUIRE(seq.getFxEnvelope(0) > 0.99f);
    REQUIRE(seq.getFxEnvelope(3) < 0.01f);

    // Advance into PPQ = 0.15 (Step 1) -> FX 3 transitions to active, FX 0 turns off
    seq.advanceSampleWithFxMask(120.0, true, true, 0.15, stepEntered, fxTransitionMask);
    REQUIRE(seq.getCurrentStep() == 1);
    REQUIRE((fxTransitionMask & (1u << 3)) != 0u);
    REQUIRE(seq.getFxEnvelope(0) < 0.01f);
    REQUIRE(seq.getFxEnvelope(3) > 0.99f);
}
