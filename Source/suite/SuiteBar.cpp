#include "SuiteBar.h"

namespace openglitch::suite {

namespace {
void style(juce::Button& button, const juce::String& tip) {
    button.setTooltip(tip);
    button.setMouseCursor(juce::MouseCursor::PointingHandCursor);
}
} // namespace

SuiteBar::SuiteBar(PresetLibrary& library, Hooks hooks)
    : library_(library), hooks_(std::move(hooks)) {
    style(undoButton_, "Undo the last edit. Cmd/Ctrl+Z.");
    style(redoButton_, "Redo. Shift+Cmd/Ctrl+Z.");
    style(aButton_, "Hear slot A.");
    style(bButton_, "Hear slot B.");
    style(copyAtoBButton_, "Copy slot A onto slot B.");
    style(copyBtoAButton_, "Copy slot B onto slot A.");
    style(saveButton_, "Save the current sound as a user preset.");
    style(favoriteButton_, "Mark or unmark the selected preset as a favorite.");
    style(editButton_, "Rename or delete the selected user preset.");
    style(previousButton_, "Previous preset");
    style(nextButton_, "Next preset");
    aButton_.setClickingTogglesState(true);
    bButton_.setClickingTogglesState(true);
    aButton_.setRadioGroupId(5101);
    bButton_.setRadioGroupId(5101);
    search_.setTextToShowWhenEmpty("Search presets", juce::Colours::grey);
    search_.setTooltip("Filter factory and user presets.");
    search_.onTextChange = [this] { rebuildList(); };
    presetBox_.setTooltip("Factory and user presets. Factory presets stay read-only.");
    presetBox_.onChange = [this] {
        if (!rebuilding_) {
            chooseListed(presetBox_.getSelectedId());
        }
    };

    undoButton_.onClick = [this] { if (hooks_.undo) hooks_.undo(); refresh(); };
    redoButton_.onClick = [this] { if (hooks_.redo) hooks_.redo(); refresh(); };
    aButton_.onClick = [this] { if (hooks_.selectA && hooks_.abIsA && !hooks_.abIsA()) hooks_.selectA(); refresh(); };
    bButton_.onClick = [this] { if (hooks_.selectB && hooks_.abIsA && hooks_.abIsA()) hooks_.selectB(); refresh(); };
    copyAtoBButton_.onClick = [this] { if (hooks_.copyAtoB) hooks_.copyAtoB(); refresh(); };
    copyBtoAButton_.onClick = [this] { if (hooks_.copyBtoA) hooks_.copyBtoA(); refresh(); };
    // Steps through the listed presets and wraps. With no listed preset loaded,
    // Next starts at the first entry and Previous at the last.
    auto step = [this](int delta) {
        const int count = static_cast<int>(shown_.size());
        if (count == 0) {
            return;
        }
        const int from = std::max(presetBox_.getSelectedId() - 1, delta > 0 ? -1 : 0);
        chooseListed((from + delta + count) % count + 1);
    };
    previousButton_.onClick = [step] { step(-1); };
    nextButton_.onClick = [step] { step(1); };
    saveButton_.onClick = [this] {
        auto* window = new juce::AlertWindow("Save preset", "Saved presets go to Documents/Alan Marcero.", juce::AlertWindow::NoIcon);
        window->addTextEditor("name", hooks_.currentLabel ? hooks_.currentLabel() : juce::String(), "Name");
        window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
        window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        const juce::Component::SafePointer<SuiteBar> safe(this);
        window->enterModalState(true, juce::ModalCallbackFunction::create([safe, window](int result) {
            if (safe == nullptr || result != 1 || safe->hooks_.saveUser == nullptr) {
                return;
            }
            juce::String error;
            if (!safe->hooks_.saveUser(window->getTextEditorContents("name"), error)) {
                safe->showError(error);
            }
            safe->refresh();
        }), true);
    };
    favoriteButton_.onClick = [this] {
        const auto* entry = selectedEntry();
        if (entry == nullptr) {
            return;
        }
        library_.setFavorite(entry->name, entry->factory, !entry->favorite);
        refresh();
    };
    editButton_.onClick = [this] {
        const auto* entry = selectedEntry();
        juce::PopupMenu menu;
        const bool user = entry != nullptr && !entry->factory;
        menu.addItem(1, "Rename", user);
        menu.addItem(2, "Delete", user);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&editButton_),
                           [safe = juce::Component::SafePointer<SuiteBar>(this)](int result) {
            if (safe == nullptr || result == 0) {
                return;
            }
            const auto* chosen = safe->selectedEntry();
            if (chosen == nullptr || chosen->factory) {
                safe->showError("Factory presets stay read-only.");
                return;
            }
            const auto name = chosen->name;
            if (result == 2) {
                juce::String error;
                if (safe->hooks_.deleteUser && !safe->hooks_.deleteUser(name, error)) {
                    safe->showError(error);
                }
                safe->refresh();
                return;
            }
            auto* window = new juce::AlertWindow("Rename preset", "Factory names stay read-only.", juce::AlertWindow::NoIcon);
            window->addTextEditor("name", name, "Name");
            window->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
            window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            window->enterModalState(true, juce::ModalCallbackFunction::create([safe, window, name](int renameResult) {
                if (safe == nullptr || renameResult != 1 || safe->hooks_.renameUser == nullptr) {
                    return;
                }
                juce::String error;
                if (!safe->hooks_.renameUser(name, window->getTextEditorContents("name"), error)) {
                    safe->showError(error);
                }
                safe->refresh();
            }), true);
        });
    };

    for (auto* button : { &undoButton_, &redoButton_, &aButton_, &bButton_, &copyAtoBButton_, &copyBtoAButton_,
                          &previousButton_, &nextButton_, &saveButton_, &favoriteButton_, &editButton_ }) {
        addAndMakeVisible(button);
    }
    addAndMakeVisible(search_);
    addAndMakeVisible(presetBox_);
    refresh();
}

const PresetLibrary::Entry* SuiteBar::selectedEntry() const {
    const int id = presetBox_.getSelectedId();
    if (id <= 0 || id > static_cast<int>(shown_.size())) {
        return nullptr;
    }
    return &shown_[static_cast<size_t>(id - 1)];
}

void SuiteBar::showError(const juce::String& error) {
    if (error.isNotEmpty()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Preset", error);
    }
}

void SuiteBar::chooseListed(int comboId) {
    if (comboId <= 0 || comboId > static_cast<int>(shown_.size())) {
        return;
    }
    const auto& entry = shown_[static_cast<size_t>(comboId - 1)];
    if (entry.factory) {
        if (hooks_.loadFactory) {
            hooks_.loadFactory(entry.factoryIndex);
        }
    } else if (hooks_.loadUser) {
        hooks_.loadUser(entry.name);
    }
    refresh();
}

void SuiteBar::rebuildList() {
    const juce::ScopedValueSetter<bool> guard(rebuilding_, true);
    shown_ = library_.entries(search_.getText());
    presetBox_.clear(juce::dontSendNotification);
    juce::String previousGroup;
    for (int i = 0; i < static_cast<int>(shown_.size()); ++i) {
        const auto& entry = shown_[static_cast<size_t>(i)];
        if (entry.group != previousGroup) {
            presetBox_.addSectionHeading(entry.group);
            previousGroup = entry.group;
        }
        const auto label = (entry.favorite ? "* " : "") + entry.name;
        presetBox_.addItem(label, i + 1);
    }
}

void SuiteBar::refresh() {
    rebuildList();
    const bool slotA = hooks_.abIsA == nullptr || hooks_.abIsA();
    aButton_.setToggleState(slotA, juce::dontSendNotification);
    bButton_.setToggleState(!slotA, juce::dontSendNotification);
    const auto label = hooks_.currentLabel ? hooks_.currentLabel() : juce::String();
    int selected = 0;
    if (hooks_.currentIsFactory && hooks_.currentIsFactory() && hooks_.currentFactoryIndex) {
        const int index = hooks_.currentFactoryIndex();
        for (int i = 0; i < static_cast<int>(shown_.size()); ++i) {
            if (shown_[static_cast<size_t>(i)].factory && shown_[static_cast<size_t>(i)].factoryIndex == index) {
                selected = i + 1;
                break;
            }
        }
    } else {
        for (int i = 0; i < static_cast<int>(shown_.size()); ++i) {
            if (!shown_[static_cast<size_t>(i)].factory && shown_[static_cast<size_t>(i)].name == label) {
                selected = i + 1;
                break;
            }
        }
    }
    // An edit since the last load or save marks the name with " *".
    const juce::String editedMark = hooks_.currentIsEdited && hooks_.currentIsEdited() ? " *" : "";
    if (selected > 0) {
        presetBox_.changeItemText(selected, presetBox_.getItemText(selected - 1) + editedMark);
        presetBox_.setSelectedId(selected, juce::dontSendNotification);
    } else {
        presetBox_.setSelectedId(0, juce::dontSendNotification);
        presetBox_.setText((label.isNotEmpty() ? label : "Presets") + editedMark, juce::dontSendNotification);
    }
    const auto* entry = selectedEntry();
    favoriteButton_.setToggleState(entry != nullptr && entry->favorite, juce::dontSendNotification);
}

void SuiteBar::resized() {
    auto row = getLocalBounds().reduced(0, 2);
    auto take = [&](int width) {
        auto area = row.removeFromLeft(width);
        if (row.getWidth() > 0) {
            row.removeFromLeft(4);
        }
        return area;
    };
    undoButton_.setBounds(take(56));
    redoButton_.setBounds(take(56));
    aButton_.setBounds(take(44));
    bButton_.setBounds(take(44));
    copyAtoBButton_.setBounds(take(52));
    copyBtoAButton_.setBounds(take(52));
    search_.setBounds(take(168));
    previousButton_.setBounds(take(40).expanded(0, 2));
    nextButton_.setBounds(take(40).expanded(0, 2));
    saveButton_.setBounds(row.removeFromRight(56));
    row.removeFromRight(4);
    editButton_.setBounds(row.removeFromRight(52));
    row.removeFromRight(4);
    favoriteButton_.setBounds(row.removeFromRight(48));
    row.removeFromRight(4);
    presetBox_.setBounds(row);
}

} // namespace openglitch::suite
