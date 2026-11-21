#include "../Source/Plugin/FactoryPatterns.h"
#include "../Source/Plugin/Parameters.h"
#include "../Source/Plugin/PluginProcessor.h"
#include "../Source/Plugin/StepSequencerComponent.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <set>

namespace {

int activeSteps(const OpenGlitchFXProcessor& processor) {
    int count = 0;
    for (int step = 0; step < 32; ++step) {
        if (processor.parameters.getRawParameterValue(openglitch::params::stepParamId(step))->load() > 0.5f) {
            ++count;
        }
    }
    return count;
}

bool routedEffectIsAudible(const OpenGlitchFXProcessor& processor) {
    for (int step = 0; step < 32; ++step) {
        if (processor.parameters.getRawParameterValue(openglitch::params::stepParamId(step))->load() <= 0.5f) {
            continue;
        }
        const auto mask = openglitch::params::readStepFxMask(processor.parameters, step);
        for (int fx = 0; fx < 15; ++fx) {
            if ((mask & (1u << fx)) == 0) {
                continue;
            }
            const auto amount = processor.parameters.getRawParameterValue(
                openglitch::params::kFxDescriptors[static_cast<size_t>(fx)].amtParamId)->load();
            if (amount > 1.0f) {
                return true;
            }
        }
    }
    return false;
}

std::string patternKey(const OpenGlitchFXProcessor& processor) {
    std::string key;
    for (int step = 0; step < 32; ++step) {
        key += processor.parameters.getRawParameterValue(openglitch::params::stepParamId(step))->load() > 0.5f ? '1' : '0';
        key += std::to_string(openglitch::params::readStepFxMask(processor.parameters, step));
        key += ',';
    }
    return key;
}

} // namespace

TEST_CASE("Factory patterns fill the program list and open on a pattern", "[plugin][patterns]") {
    juce::ScopedJuceInitialiser_GUI gui;
    REQUIRE(openglitch::patterns::stepsAreValid());
    REQUIRE(openglitch::patterns::count() >= 30);
    REQUIRE(openglitch::patterns::count() <= 40);
    OpenGlitchFXProcessor processor;
    REQUIRE(processor.getNumPrograms() == openglitch::patterns::count());
    std::set<std::string> names;
    for (int i = 0; i < processor.getNumPrograms(); ++i) {
        const auto name = processor.getProgramName(i);
        REQUIRE(name.isNotEmpty());
        REQUIRE(names.insert(name.toStdString()).second);
    }
    REQUIRE(processor.currentPresetIsFactory());
    REQUIRE(processor.currentPresetLabel() == processor.getProgramName(0));
    REQUIRE(activeSteps(processor) >= 8);
    REQUIRE(routedEffectIsAudible(processor));
    REQUIRE_THAT(processor.parameters.getRawParameterValue("sequence_length")->load(), Catch::Matchers::WithinAbs(32.0, 0.001));
    REQUIRE_THAT(processor.parameters.getRawParameterValue("sequencer_swing")->load(), Catch::Matchers::WithinAbs(0.0, 0.001));
}

TEST_CASE("Factory programs are distinct and a repeated host program call keeps the session", "[plugin][patterns]") {
    juce::ScopedJuceInitialiser_GUI gui;
    OpenGlitchFXProcessor processor;
    std::set<std::string> keys;
    for (int i = 0; i < processor.getNumPrograms(); ++i) {
        processor.setCurrentProgram(i);
        REQUIRE(processor.getCurrentProgram() == i);
        REQUIRE(activeSteps(processor) > 0);
        REQUIRE(routedEffectIsAudible(processor));
        REQUIRE(keys.insert(patternKey(processor)).second);
    }
    auto* step = processor.parameters.getParameter(openglitch::params::stepParamId(0));
    step->setValueNotifyingHost(0.0f);
    juce::MemoryBlock saved;
    processor.getStateInformation(saved);
    OpenGlitchFXProcessor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE(restored.parameters.getRawParameterValue(openglitch::params::stepParamId(0))->load() < 0.5f);
    restored.setCurrentProgram(restored.getCurrentProgram());
    REQUIRE(restored.parameters.getRawParameterValue(openglitch::params::stepParamId(0))->load() < 0.5f);
}

TEST_CASE("Parameter gestures, pad edits, and randomize undo as one step", "[plugin][undo]") {
    juce::ScopedJuceInitialiser_GUI gui;
    OpenGlitchFXProcessor processor;
    auto& undo = processor.getUndo();
    auto* amount = processor.parameters.getParameter(openglitch::params::kBitcrushAmt);
    const float start = amount->getValue();
    amount->beginChangeGesture();
    amount->setValueNotifyingHost(0.2f);
    amount->setValueNotifyingHost(0.55f);
    amount->setValueNotifyingHost(0.9f);
    amount->endChangeGesture();
    REQUIRE(undo.canUndo());
    REQUIRE(undo.undo());
    REQUIRE_THAT(amount->getValue(), Catch::Matchers::WithinAbs(start, 0.0001));
    REQUIRE_FALSE(undo.canUndo());
    REQUIRE(undo.redo());
    REQUIRE_THAT(amount->getValue(), Catch::Matchers::WithinAbs(0.9, 0.02));
    REQUIRE(undo.undo());
    REQUIRE_FALSE(undo.canUndo());

    const auto before = patternKey(processor);
    undo.beginGesture("Edit steps");
    for (int step = 0; step < 6; ++step) {
        processor.parameters.getParameter(openglitch::params::stepParamId(step))->beginChangeGesture();
        processor.parameters.getParameter(openglitch::params::stepParamId(step))->setValueNotifyingHost(step % 2 == 0 ? 0.0f : 1.0f);
        processor.parameters.getParameter(openglitch::params::stepParamId(step))->endChangeGesture();
    }
    undo.endGesture();
    REQUIRE(patternKey(processor) != before);
    REQUIRE(undo.undo());
    REQUIRE(patternKey(processor) == before);
    REQUIRE_FALSE(undo.canUndo());

    openglitch::ui::GlitchLookAndFeel look;
    openglitch::ui::StepSequencerComponent grid(processor.parameters, look, undo);
    const auto seeded = patternKey(processor);
    undo.perform("Randomize", [&] { grid.randomizePattern(99u, 3); });
    REQUIRE(patternKey(processor) != seeded);
    REQUIRE(undo.undo());
    REQUIRE(patternKey(processor) == seeded);

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    const float beforeKey = amount->getValue();
    amount->beginChangeGesture();
    amount->setValueNotifyingHost(0.15f);
    amount->endChangeGesture();
    REQUIRE(editor->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)));
    REQUIRE_THAT(amount->getValue(), Catch::Matchers::WithinAbs(beforeKey, 0.0001));
    REQUIRE(editor->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)));
    REQUIRE_THAT(amount->getValue(), Catch::Matchers::WithinAbs(0.15, 0.02));
}

TEST_CASE("A/B slots copy and survive a session round trip", "[plugin][ab]") {
    juce::ScopedJuceInitialiser_GUI gui;
    OpenGlitchFXProcessor processor;
    auto* amount = processor.parameters.getParameter(openglitch::params::kStutterAmt);
    const float factory = amount->getValue();
    amount->beginChangeGesture();
    amount->setValueNotifyingHost(0.85f);
    amount->endChangeGesture();
    processor.copyAbAtoB();
    amount->beginChangeGesture();
    amount->setValueNotifyingHost(0.2f);
    amount->endChangeGesture();
    REQUIRE(processor.abIsA());
    processor.selectAbSlot(false);
    REQUIRE_FALSE(processor.abIsA());
    REQUIRE_THAT(amount->getValue(), Catch::Matchers::WithinAbs(0.85, 0.02));
    processor.selectAbSlot(true);
    REQUIRE_THAT(amount->getValue(), Catch::Matchers::WithinAbs(0.2, 0.02));

    juce::MemoryBlock saved;
    processor.getStateInformation(saved);
    OpenGlitchFXProcessor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE(restored.abIsA());
    REQUIRE_THAT(restored.parameters.getParameter(openglitch::params::kStutterAmt)->getValue(),
                 Catch::Matchers::WithinAbs(0.2, 0.02));
    restored.selectAbSlot(false);
    REQUIRE_THAT(restored.parameters.getParameter(openglitch::params::kStutterAmt)->getValue(),
                 Catch::Matchers::WithinAbs(0.85, 0.02));
    restored.selectAbSlot(true);
    restored.copyAbBtoA();
    REQUIRE_THAT(restored.parameters.getParameter(openglitch::params::kStutterAmt)->getValue(),
                 Catch::Matchers::WithinAbs(0.85, 0.02));
    REQUIRE(factory >= 0.0f);
}

TEST_CASE("Each A/B slot keeps its own undo history and preset name", "[plugin][ab]") {
    juce::ScopedJuceInitialiser_GUI gui;
    OpenGlitchFXProcessor processor;
    auto& undo = processor.getUndo();
    auto* amount = processor.parameters.getParameter(openglitch::params::kStutterAmt);
    auto edit = [&](float value) {
        amount->beginChangeGesture();
        amount->setValueNotifyingHost(value);
        amount->endChangeGesture();
    };
    processor.loadFactoryProgram(1);
    const float loadedA = amount->getValue();
    REQUIRE_FALSE(processor.currentPresetIsEdited());
    edit(0.3f);
    REQUIRE(processor.currentPresetIsEdited());
    processor.selectAbSlot(false);
    REQUIRE_FALSE(undo.canUndo());
    processor.loadFactoryProgram(4);
    const float loadedB = amount->getValue();
    edit(0.6f);
    REQUIRE(undo.undo());
    REQUIRE_THAT(amount->getValue(), Catch::Matchers::WithinAbs(loadedB, 1e-4));

    processor.selectAbSlot(true);
    REQUIRE(processor.currentPresetLabel() == "Straight Gate");
    REQUIRE(processor.currentPresetIsEdited());
    REQUIRE(undo.undo());
    REQUIRE_THAT(amount->getValue(), Catch::Matchers::WithinAbs(loadedA, 1e-4));
    REQUIRE_FALSE(processor.currentPresetIsEdited());
    juce::MemoryBlock saved;
    processor.getStateInformation(saved);
    OpenGlitchFXProcessor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    restored.selectAbSlot(false);
    REQUIRE(restored.currentPresetLabel() == "Tape Stop Fill");
    processor.copyAbAtoB();
    processor.selectAbSlot(false);

    REQUIRE_FALSE(undo.canUndo());
    REQUIRE(processor.currentPresetLabel() == "Straight Gate");
}

TEST_CASE("User presets save, search, favorite, rename, and leave factory presets read-only", "[plugin][presets]") {
    juce::ScopedJuceInitialiser_GUI gui;
    OpenGlitchFXProcessor processor;
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("openglitch-presets-" + juce::String(juce::Random::getSystemRandom().nextInt64()));
    directory.deleteRecursively();
    processor.getPresets().setDirectoryForTests(directory);

    auto* amount = processor.parameters.getParameter(openglitch::params::kChopperAmt);
    amount->beginChangeGesture();
    amount->setValueNotifyingHost(0.77f);
    amount->endChangeGesture();
    juce::String error;
    REQUIRE(processor.saveUserPreset("Booth Chop", error));
    REQUIRE(error.isEmpty());
    REQUIRE_FALSE(processor.saveUserPreset("Club Stutter", error));
    REQUIRE(error.isNotEmpty());
    REQUIRE_FALSE(processor.deleteUserPreset("Club Stutter", error));
    REQUIRE_FALSE(processor.renameUserPreset("Club Stutter", "Renamed", error));

    amount->setValueNotifyingHost(0.05f);
    processor.loadUserPreset("Booth Chop");
    REQUIRE_THAT(amount->getValue(), Catch::Matchers::WithinAbs(0.77, 0.02));
    REQUIRE_FALSE(processor.currentPresetIsFactory());
    REQUIRE(processor.currentPresetLabel() == "Booth Chop");

    int matches = 0;
    for (const auto& entry : processor.getPresets().entries("booth")) {
        if (!entry.factory && entry.name == "Booth Chop") {
            ++matches;
        }
    }
    REQUIRE(matches == 1);
    processor.getPresets().setFavorite("Booth Chop", false, true);
    REQUIRE(processor.getPresets().isFavorite("Booth Chop", false));
    processor.getPresets().setFavorite("Club Stutter", true, true);
    REQUIRE(processor.getPresets().isFavorite("Club Stutter", true));

    REQUIRE(processor.renameUserPreset("Booth Chop", "Night Chop", error));
    REQUIRE(processor.getPresets().loadUser("Night Chop").isValid());
    REQUIRE_FALSE(processor.getPresets().loadUser("Booth Chop").isValid());
    REQUIRE(processor.deleteUserPreset("Night Chop", error));
    REQUIRE_FALSE(processor.getPresets().loadUser("Night Chop").isValid());
    directory.deleteRecursively();
}
