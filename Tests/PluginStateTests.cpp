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

TEST_CASE("Editor renders native UI screenshot", "[plugin][gui]") {
    juce::ScopedJuceInitialiser_GUI gui;
    OpenGlitchFXProcessor processor;
    processor.prepareToPlay(48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    REQUIRE(editor != nullptr);
    REQUIRE(editor->getWidth() == 1440);
    REQUIRE(editor->getHeight() == 1120);
    auto hasVisibleLabel = [&](const juce::String& text) {
        for (auto* child : editor->getChildren()) {
            if (auto* label = dynamic_cast<juce::Label*>(child);
                label != nullptr && label->getText() == text && label->isVisible() && !label->getBounds().isEmpty())
                return true;
        }
        return false;
    };
    REQUIRE(hasVisibleLabel("LENGTH"));
    REQUIRE(hasVisibleLabel("SWING"));
    REQUIRE(hasVisibleLabel("32 STEPS"));
    REQUIRE(hasVisibleLabel("0%"));
    const auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds(), false, 2.0f);
    REQUIRE(snapshot.isValid());
    REQUIRE(snapshot.getWidth() == 2880);
    REQUIRE(snapshot.getHeight() == 2240);
    juce::File cwd = juce::File::getCurrentWorkingDirectory();
    juce::File file = cwd.getChildFile("assets/native-ui.png");
    if (!cwd.getChildFile("assets").isDirectory() && cwd.getParentDirectory().getChildFile("assets").isDirectory())
        file = cwd.getParentDirectory().getChildFile("assets/native-ui.png");
    else
        file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream stream(file);
    REQUIRE(stream.openedOk());
    juce::PNGImageFormat png;
    REQUIRE(png.writeImageToStream(snapshot, stream));
}

TEST_CASE("Sequencer length and swing are automatable and survive state restore", "[plugin][sequencer]") {
    juce::ScopedJuceInitialiser_GUI gui;
    OpenGlitchFXProcessor processor;
    auto* length = processor.parameters.getParameter("sequence_length");
    auto* swing = processor.parameters.getParameter("sequencer_swing");
    REQUIRE(length != nullptr);
    REQUIRE(swing != nullptr);
    REQUIRE(length->isAutomatable());
    REQUIRE(swing->isAutomatable());
    REQUIRE_THAT(processor.parameters.getRawParameterValue("sequence_length")->load(),
                 Catch::Matchers::WithinAbs(32.0f, 0.001f));
    REQUIRE_THAT(processor.parameters.getRawParameterValue("sequencer_swing")->load(),
                 Catch::Matchers::WithinAbs(0.0f, 0.001f));

    length->setValueNotifyingHost(length->convertTo0to1(16.0f));
    swing->setValueNotifyingHost(swing->convertTo0to1(70.0f));
    juce::MemoryBlock saved;
    processor.getStateInformation(saved);
    OpenGlitchFXProcessor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));

    REQUIRE_THAT(restored.parameters.getRawParameterValue("sequence_length")->load(),
                 Catch::Matchers::WithinAbs(16.0f, 0.001f));
    REQUIRE_THAT(restored.parameters.getRawParameterValue("sequencer_swing")->load(),
                 Catch::Matchers::WithinAbs(70.0f, 0.001f));
}

TEST_CASE("Pattern randomization is repeatable and selected-FX randomization preserves other routes", "[plugin][sequencer]") {
    juce::ScopedJuceInitialiser_GUI gui;
    OpenGlitchFXProcessor firstProcessor;
    OpenGlitchFXProcessor secondProcessor;
    openglitch::ui::GlitchLookAndFeel look;
    openglitch::ui::StepSequencerComponent first(firstProcessor.parameters, look);
    openglitch::ui::StepSequencerComponent second(secondProcessor.parameters, look);
    for (int step = 0; step < 32; ++step) {
        firstProcessor.parameters.getParameter(openglitch::params::stepParamId(step))->setValueNotifyingHost(1.0f);
        secondProcessor.parameters.getParameter(openglitch::params::stepParamId(step))->setValueNotifyingHost(1.0f);
        firstProcessor.parameters.getParameter(openglitch::params::stepFxParamId(step))->setValueNotifyingHost(1.0f);
        secondProcessor.parameters.getParameter(openglitch::params::stepFxParamId(step))->setValueNotifyingHost(1.0f);
        firstProcessor.parameters.getParameter(openglitch::params::stepExtraFxParamId(step))->setValueNotifyingHost(1.0f);
        secondProcessor.parameters.getParameter(openglitch::params::stepExtraFxParamId(step))->setValueNotifyingHost(1.0f);
    }

    constexpr int selectedFx = 4;
    const auto selectedBit = static_cast<uint16_t>(1u << selectedFx);
    first.setPaintTargetFx(selectedFx);
    second.setPaintTargetFx(selectedFx);
    first.randomizePattern(71021u, 2);
    second.randomizePattern(71021u, 2);

    int selectedFxOnPads = 0;
    int selectedFxOffPads = 0;
    for (int step = 0; step < 32; ++step) {
        const auto firstMask = openglitch::params::readStepFxMask(firstProcessor.parameters, step);
        const auto secondMask = openglitch::params::readStepFxMask(secondProcessor.parameters, step);
        REQUIRE(firstMask == secondMask);
        REQUIRE((firstMask & static_cast<uint16_t>(~selectedBit)) ==
                (openglitch::core::BeatStepSequencer::kAllFxMask & static_cast<uint16_t>(~selectedBit)));
        REQUIRE(firstProcessor.parameters.getRawParameterValue(openglitch::params::stepParamId(step))->load() > 0.5f);
        selectedFxOnPads += (firstMask & selectedBit) != 0u ? 1 : 0;
        selectedFxOffPads += (firstMask & selectedBit) == 0u ? 1 : 0;
    }
    REQUIRE(selectedFxOnPads > 0);
    REQUIRE(selectedFxOffPads > 0);
}

TEST_CASE("Full-pattern randomization is seeded, keeps routes in the selected complexity band, and leaves no silent pads", "[plugin][sequencer]") {
    juce::ScopedJuceInitialiser_GUI gui;
    OpenGlitchFXProcessor firstProcessor;
    OpenGlitchFXProcessor secondProcessor;
    openglitch::ui::GlitchLookAndFeel look;
    openglitch::ui::StepSequencerComponent first(firstProcessor.parameters, look);
    openglitch::ui::StepSequencerComponent second(secondProcessor.parameters, look);

    first.randomizePattern(193117u, 1);
    second.randomizePattern(193117u, 1);

    int activeSteps = 0;
    for (int step = 0; step < 32; ++step) {
        const bool activeFirst = firstProcessor.parameters.getRawParameterValue(openglitch::params::stepParamId(step))->load() > 0.5f;
        const bool activeSecond = secondProcessor.parameters.getRawParameterValue(openglitch::params::stepParamId(step))->load() > 0.5f;
        const auto maskFirst = openglitch::params::readStepFxMask(firstProcessor.parameters, step);
        const auto maskSecond = openglitch::params::readStepFxMask(secondProcessor.parameters, step);
        REQUIRE(activeFirst == activeSecond);
        REQUIRE(maskFirst == maskSecond);
        if (activeFirst) {
            ++activeSteps;
            REQUIRE(maskFirst != 0u);
            REQUIRE((maskFirst & static_cast<uint16_t>(~0x001fu)) == 0u);
        } else {
            REQUIRE(maskFirst == 0u);
        }
    }
    REQUIRE(activeSteps >= 6);
    REQUIRE(activeSteps <= 18);
}
