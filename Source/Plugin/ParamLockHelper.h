#pragma once

#include "PluginProcessor.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace openglitch::ui {

/** Attaches right-click lock menus and draws non-intercepting padlock badges for parameter controls. */
class ParamLockOverlayHelper final : public juce::MouseListener {
public:
    struct Entry {
        juce::Component* component = nullptr;
        std::function<juce::String()> getParamId;
        bool isComboBox = false;
    };

    ParamLockOverlayHelper(OpenGlitchFXProcessor& processor, juce::Component& host);
    ~ParamLockOverlayHelper() override;

    /** Register a control whose parameter ID never changes. */
    void registerControl(juce::Component& control, const juce::String& paramId, bool isComboBox = false);

    /** Register a control whose parameter ID is resolved dynamically (e.g., shared envelope tabs). */
    void registerDynamicControl(juce::Component& control,
                                std::function<juce::String()> getParamId,
                                bool isComboBox = false);

    void mouseDown(const juce::MouseEvent& event) override;

    /** Paint padlock badges over all visible locked controls within host_. */
    void paintLocks(juce::Graphics& g, const juce::Colour& accent, const juce::Colour& bg) const;

    /** Draw a crisp vector padlock icon inside iconBounds. */
    static void drawPadlock(juce::Graphics& g,
                            juce::Rectangle<float> iconBounds,
                            const juce::Colour& accent,
                            const juce::Colour& bg);

private:
    const Entry* findEntryFor(const juce::Component* source) const;

    OpenGlitchFXProcessor& processor_;
    juce::Component& host_;
    std::vector<Entry> entries_;
};

} // namespace openglitch::ui
