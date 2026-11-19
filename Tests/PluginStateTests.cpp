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
    openglitch::ui::StepSequencerComponent grid(processor.parameters, look, processor.getUndo());
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
    processor.parameters.getParameter(openglitch::params::kPingPongOn)->setValueNotifyingHost(1.0f);
    auto* amount = processor.parameters.getParameter(openglitch::params::kPingPongAmt);
    amount->setValueNotifyingHost(1.0f);
    REQUIRE(processor.getTailLengthSeconds() >= 39.0);
    amount->setValueNotifyingHost(0.0f);
    REQUIRE_THAT(processor.getTailLengthSeconds(), Catch::Matchers::WithinAbs(0, 0));
}

TEST_CASE("Slice register follows the transport and internal sequencer", "[plugin][gui]") {
    juce::ScopedJuceInitialiser_GUI gui;
    struct Host final : juce::AudioPlayHead {
        PositionInfo info;
        juce::Optional<PositionInfo> getPosition() const override { return info; }
    } host;
    OpenGlitchFXProcessor processor;
    processor.setPlayHead(&host);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block(2, 256);
    auto image = [&] { return editor->createComponentSnapshot({393, 12, 128, 32}); };
    auto differences = [](const juce::Image& a, const juce::Image& b) {
        int count = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) ++count;
        return count;
    };
    for (double rate : {48000.0, 96000.0}) {
        processor.prepareToPlay(rate, 256);
        processor.setPlayHead(&host);
        auto advance = [&] { block.clear(); processor.processBlock(block, midi); };
        host.info.setIsPlaying(true); host.info.setPpqPosition(-2.0); host.info.setBpm(90);
        advance();
        const auto first = image();
        REQUIRE_THAT(processor.getVisualBeat(), Catch::Matchers::WithinAbs(255.0 * 1.5 / rate, 0.00001));
        host.info.setPpqPosition(13.75); host.info.setBpm(180); advance();
        REQUIRE_THAT(processor.getVisualBeat(), Catch::Matchers::WithinAbs(.75 + 255.0 * 3.0 / rate, 0.00001));
        const auto seek = image();
        REQUIRE(differences(first, seek) > 250);
        host.info.setIsPlaying(false); host.info.setPpqPosition(18.0); advance();
        REQUIRE(differences(seek, image()) == 0);
        processor.setPlayHead(nullptr); advance();
        const auto phase = processor.getVisualBeat();
        advance();
        const double bpm = processor.parameters.getRawParameterValue(openglitch::params::kInternalBpm)->load();
        REQUIRE_THAT(processor.getVisualBeat(), Catch::Matchers::WithinAbs(phase + 256.0 * bpm / (60.0 * rate), 0.00001));
    }
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
    editor->setVisible(true);
    int targets = 0;
    auto check = [&](auto&& self, juce::Component& parent) -> void {
        for (auto* c : parent.getChildren()) {
            if (!c->isVisible()) continue;
            if (dynamic_cast<juce::Button*>(c) || dynamic_cast<juce::ComboBox*>(c)) {
                const auto area = editor->getLocalArea(c, c->getLocalBounds());
                CAPTURE(c->getName().toStdString(), area.toString().toStdString());
                REQUIRE(area.getHeight() >= 36);
                REQUIRE(area.getWidth() >= 44);
                REQUIRE(editor->getLocalBounds().contains(area));
                auto* hit = editor->getComponentAt(area.getCentre());
                REQUIRE((hit == c || c->isParentOf(hit)));
                ++targets;
            }
            self(self, *c);
        }
    };
    for (const auto size : {juce::Point<int>(1300, 1050), juce::Point<int>(1440, 1120)}) {
        editor->setSize(size.x, size.y);
        check(check, *editor);
    }
    REQUIRE(targets >= 90);
    juce::ComboBox* themes = nullptr;
    for (auto* c : editor->getChildren())
        if (auto* box = dynamic_cast<juce::ComboBox*>(c); box && box->getItemText(0) == "Cyber Neon")
            themes = box;
    REQUIRE(themes != nullptr);
    for (int i = 0; i < 4; ++i) {
        themes->setSelectedId(i + 1, juce::sendNotificationSync);
        auto& look = editor->getLookAndFeel();
        REQUIRE(look.findColour(juce::PopupMenu::backgroundColourId).getBrightness() < 0.42f);
        REQUIRE(look.findColour(juce::TooltipWindow::textColourId).getBrightness() > 0.72f);
        int width = 0, height = 0;
        look.getIdealPopupMenuItemSize("Pad routing", false, 0, width, height);
        REQUIRE(height >= 36);
        if (auto* directory = std::getenv("OPENGLITCH_THEME_CAPTURES")) {
            const auto file = juce::File(directory).getChildFile("theme-" + juce::String(i + 1) + ".png");
            juce::FileOutputStream stream(file); REQUIRE(stream.openedOk());
            stream.setPosition(0); stream.truncate();
            REQUIRE(juce::PNGImageFormat{}.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), stream));
        }
    }
    themes->setSelectedId(1, juce::sendNotificationSync);
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
    const auto artifact = juce::File("/opt/cursor/artifacts/ui.png");
    artifact.getParentDirectory().createDirectory();
    artifact.deleteFile();
    juce::FileOutputStream artifactStream(artifact);
    REQUIRE(artifactStream.openedOk());
    REQUIRE(png.writeImageToStream(snapshot, artifactStream));
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

    auto* patternRate = processor.parameters.getParameter("pattern_rate");
    auto* patternChance = processor.parameters.getParameter("pattern_chance");
    auto* keepSub = processor.parameters.getParameter("keep_sub");
    REQUIRE(patternRate != nullptr);
    REQUIRE(patternChance != nullptr);
    REQUIRE(keepSub != nullptr);
    patternRate->setValueNotifyingHost(patternRate->convertTo0to1(2.0f));
    patternChance->setValueNotifyingHost(patternChance->convertTo0to1(65.0f));
    keepSub->setValueNotifyingHost(1.0f);
    juce::MemoryBlock savedEnhancements;
    processor.getStateInformation(savedEnhancements);
    OpenGlitchFXProcessor restoredEnhancements;
    restoredEnhancements.setStateInformation(savedEnhancements.getData(), static_cast<int>(savedEnhancements.getSize()));
    REQUIRE_THAT(restoredEnhancements.parameters.getRawParameterValue("pattern_rate")->load(),
                 Catch::Matchers::WithinAbs(2.0f, 0.001f));
    REQUIRE_THAT(restoredEnhancements.parameters.getRawParameterValue("pattern_chance")->load(),
                 Catch::Matchers::WithinAbs(65.0f, 0.001f));
    REQUIRE(restoredEnhancements.parameters.getRawParameterValue("keep_sub")->load() > 0.5f);
}

TEST_CASE("Pattern randomization is repeatable and selected-FX randomization preserves other routes", "[plugin][sequencer]") {
    juce::ScopedJuceInitialiser_GUI gui;
    OpenGlitchFXProcessor firstProcessor;
    OpenGlitchFXProcessor secondProcessor;
    openglitch::ui::GlitchLookAndFeel look;
    openglitch::ui::StepSequencerComponent first(firstProcessor.parameters, look, firstProcessor.getUndo());
    openglitch::ui::StepSequencerComponent second(secondProcessor.parameters, look, secondProcessor.getUndo());
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
    openglitch::ui::StepSequencerComponent first(firstProcessor.parameters, look, firstProcessor.getUndo());
    openglitch::ui::StepSequencerComponent second(secondProcessor.parameters, look, secondProcessor.getUndo());

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
