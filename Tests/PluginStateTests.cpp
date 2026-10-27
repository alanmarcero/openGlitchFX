#include "../Source/Plugin/PluginProcessor.h"
#include "../Source/Plugin/Parameters.h"
#include "../Source/Plugin/StepSequencerComponent.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

TEST_CASE("New FX routing and amounts survive a plugin state round trip", "[plugin][state]") {
    juce::ScopedJuceInitialiser_GUI gui;
    OpenGlitchFXProcessor processor;
    auto set = [&](const juce::String& id, float value) {
        auto* parameter = processor.parameters.getParameter(id);
        REQUIRE(parameter != nullptr);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    };
    using namespace openglitch::params;
    for (int fx = 0; fx < 15; ++fx) {
        set(stepFxParamId(fx), fx < 10 ? static_cast<float>(1u << fx) : 0);
        set(stepExtraFxParamId(fx), fx >= 10 ? static_cast<float>(1u << (fx - 10)) : 0);
        set(kFxDescriptors[static_cast<size_t>(fx)].amtParamId, 75);
    }
    juce::MemoryBlock saved;
    processor.getStateInformation(saved);
    OpenGlitchFXProcessor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    for (int fx = 0; fx < 15; ++fx) {
        REQUIRE(readStepFxMask(restored.parameters, fx) == (1u << fx));
        REQUIRE_THAT(restored.parameters.getRawParameterValue(kFxDescriptors[static_cast<size_t>(fx)].amtParamId)->load(),
                     Catch::Matchers::WithinAbs(75, 1e-4));
    }
}

TEST_CASE("Legacy sessions reset absent new FX and retain original routing automation", "[plugin][state]") {
    juce::ScopedJuceInitialiser_GUI gui;
    OpenGlitchFXProcessor processor;
    using namespace openglitch::params;
    auto* mask = processor.parameters.getParameter(stepFxParamId(0));
    mask->setValueNotifyingHost(mask->convertTo0to1(512));
    REQUIRE_THAT(mask->convertFrom0to1(1.0f), Catch::Matchers::WithinAbs(1023, 0));
    processor.parameters.getParameter(stepFxParamId(1))->setValueNotifyingHost(0.0f);
    processor.parameters.getParameter(stepParamId(1))->setValueNotifyingHost(1.0f);
    processor.parameters.getParameter(kFilterMode)->setValueNotifyingHost(0.5f);
    auto* cutoff = processor.parameters.getParameter(kFilterCutoff);
    cutoff->setValueNotifyingHost(cutoff->convertTo0to1(100.0f));
    auto legacy = processor.parameters.copyState();
    for (auto* parameter : processor.getParameters()) {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
        if (ranged != nullptr && ranged->getVersionHint() == 2) {
            legacy.removeChild(legacy.getChildWithProperty("id", ranged->paramID), nullptr);
            ranged->setValueNotifyingHost(1);
        }
    }
    juce::MemoryBlock saved;
    juce::AudioProcessor::copyXmlToBinary(*legacy.createXml(), saved);
    processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE_THAT(processor.parameters.getRawParameterValue(stepFxParamId(0))->load(), Catch::Matchers::WithinAbs(512, 0));
    for (size_t fx = 10; fx < kFxDescriptors.size(); ++fx) {
        REQUIRE_THAT(processor.parameters.getRawParameterValue(kFxDescriptors[fx].amtParamId)->load(), Catch::Matchers::WithinAbs(0, 0));
    }
    REQUIRE(readStepFxMask(processor.parameters, 0) == 512u);
    REQUIRE(readStepFxMask(processor.parameters, 1) == 0u);
    processor.prepareToPlay(48000, 4096);
    juce::AudioBuffer<float> audio(2, 4096);
    std::array<float, 4096> expected{};
    for (int i = 0; i < 4096; ++i) {
        const float input = 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * static_cast<float>(i) / 12.0f);
        expected[static_cast<size_t>(i)] = input;
        audio.setSample(0, i, input);
        audio.setSample(1, i, input);
    }
    juce::MidiBuffer midi;
    processor.processBlock(audio, midi);
    for (int i = 0; i < 4096; ++i) {
        REQUIRE_THAT(audio.getSample(0, i), Catch::Matchers::WithinAbs(expected[static_cast<size_t>(i)], 1e-6));
    }
    processor.releaseResources();
}

TEST_CASE("Pad brush paints and wakes every effect including the third row", "[plugin][routing]") {
    juce::ScopedJuceInitialiser_GUI gui;
    OpenGlitchFXProcessor processor;
    openglitch::ui::GlitchLookAndFeel look;
    openglitch::ui::StepSequencerComponent grid(processor.parameters, look);
    for (int fx = 0; fx < 15; ++fx) {
        INFO("FX slot " << fx);
        grid.setPaintTargetFx(fx);
        grid.applyPreset(openglitch::core::StepPatternPreset::LastFourSteps);
        for (int step = 28; step < 32; ++step) REQUIRE(openglitch::params::readStepFxMask(processor.parameters, step) == (1u << fx));
        REQUIRE(processor.parameters.getRawParameterValue(openglitch::params::kFxDescriptors[static_cast<size_t>(fx)].amtParamId)->load() > 0.0f);
    }
}

TEST_CASE("The host is told to render the enabled echo tail", "[plugin][tail]") {
    juce::ScopedJuceInitialiser_GUI gui;
    OpenGlitchFXProcessor processor;
    auto* amount = processor.parameters.getParameter(openglitch::params::kPingPongAmt);
    amount->setValueNotifyingHost(1.0f);
    REQUIRE(processor.getTailLengthSeconds() >= 39.0);
    amount->setValueNotifyingHost(0.0f);
    REQUIRE_THAT(processor.getTailLengthSeconds(), Catch::Matchers::WithinAbs(0, 0));
}
