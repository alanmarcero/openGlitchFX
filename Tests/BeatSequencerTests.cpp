#include "../Source/Core/BeatStepSequencer.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace openglitch::core;

TEST_CASE("BeatStepSequencer maps 4/4 bar PPQ to 32 steps accurately", "[sequencer]") {
    double phase = 0.0;

    // Start of bar = step 0
    REQUIRE(BeatStepSequencer::computeStepFromPpq(0.0, &phase) == 0);
    REQUIRE_THAT(phase, Catch::Matchers::WithinAbs(0.0, 1e-6));

    // Beat 2 start (1.0 PPQ) = step 8
    REQUIRE(BeatStepSequencer::computeStepFromPpq(1.0, &phase) == 8);
    REQUIRE_THAT(phase, Catch::Matchers::WithinAbs(0.25, 1e-6));

    // Beat 3 start (2.0 PPQ) = step 16
    REQUIRE(BeatStepSequencer::computeStepFromPpq(2.0, &phase) == 16);
    REQUIRE_THAT(phase, Catch::Matchers::WithinAbs(0.5, 1e-6));

    // Beat 4 start (3.0 PPQ) = step 24
    REQUIRE(BeatStepSequencer::computeStepFromPpq(3.0, &phase) == 24);
    REQUIRE_THAT(phase, Catch::Matchers::WithinAbs(0.75, 1e-6));

    // Last 1/32 of bar (3.875 PPQ) = step 31
    REQUIRE(BeatStepSequencer::computeStepFromPpq(3.88, &phase) == 31);

    // Next bar wraparound (4.0 PPQ) = step 0
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
    seq.prepare(48000.0);
    seq.setSmoothingTimeMs(0.1f);
    seq.applyPreset(StepPatternPreset::Last32nd); // Only step 31 enabled

    bool enteredActive = false;
    float envInactive = seq.advanceSample(120.0, true, true, 0.5, enteredActive);
    REQUIRE_FALSE(enteredActive);
    REQUIRE(seq.getCurrentStep() == 4);
    REQUIRE(envInactive < 0.05f);

    // Jump into step 31 (PPQ = 3.90)
    float envActive = seq.advanceSample(120.0, true, true, 3.90, enteredActive);
    REQUIRE(enteredActive);
    REQUIRE(seq.getCurrentStep() == 31);
    REQUIRE(envActive > 0.0f);
}
