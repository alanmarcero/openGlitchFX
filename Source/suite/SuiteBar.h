#pragma once

#include "PresetLibrary.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace openglitch::suite {

// Compact undo, A/B, and preset controls. The host plugin supplies the actions.
class SuiteBar final : public juce::Component {
public:
    struct Hooks {
        std::function<void()> undo;
        std::function<void()> redo;
        std::function<bool()> abIsA;
        std::function<void()> selectA;
        std::function<void()> selectB;
        std::function<void()> copyAtoB;
        std::function<void()> copyBtoA;
        std::function<void(int index)> loadFactory;
        std::function<void(const juce::String& name)> loadUser;
        std::function<bool(const juce::String& name, juce::String& error)> saveUser;
        std::function<bool(const juce::String& from, const juce::String& to, juce::String& error)> renameUser;
        std::function<bool(const juce::String& name, juce::String& error)> deleteUser;
        std::function<juce::String()> currentLabel;
        std::function<bool()> currentIsFactory;
        std::function<bool()> currentIsEdited;
        std::function<int()> currentFactoryIndex;
    };

    SuiteBar(PresetLibrary& library, Hooks hooks);

    void refresh();
    void resized() override;

private:
    void rebuildList();
    void chooseListed(int comboId);
    void showError(const juce::String& error);
    const PresetLibrary::Entry* selectedEntry() const;

    PresetLibrary& library_;
    Hooks hooks_;
    std::vector<PresetLibrary::Entry> shown_;
    bool rebuilding_ = false;

    juce::TextButton undoButton_{ "UNDO" };
    juce::TextButton redoButton_{ "REDO" };
    juce::TextButton aButton_{ "A" };
    juce::TextButton bButton_{ "B" };
    juce::TextButton copyAtoBButton_{ "A>B" };
    juce::TextButton copyBtoAButton_{ "B>A" };
    juce::TextEditor search_;
    juce::TextButton previousButton_{ "<" };
    juce::TextButton nextButton_{ ">" };
    juce::ComboBox presetBox_;
    juce::TextButton saveButton_{ "SAVE" };
    juce::TextButton favoriteButton_{ "FAV" };
    juce::TextButton editButton_{ "EDIT" };
};

} // namespace openglitch::suite
