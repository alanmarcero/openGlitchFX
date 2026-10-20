#pragma once

#include "EnvelopeEditorComponent.h"
#include "GlitchLookAndFeel.h"
#include "PluginProcessor.h"
#include <array>
#include <juce_gui_basics/juce_gui_basics.h>

namespace openglitch::ui {

// Controls below the FX cards
class DetailPanelComponent final : public juce::Component {
public:
    DetailPanelComponent(OpenGlitchFXProcessor& processor, const GlitchLookAndFeel& lnf);

    void setTelemetry(const openglitch::core::MeterTelemetry& telemetry);
    void refreshFromState();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Card {
        juce::String title;
        juce::String subtitle;
        juce::Rectangle<float> bounds;
    };

    void setupCombo(juce::ComboBox& box, const char* paramId, const juce::String& caption, const juce::String& tooltip);
    void setupToggle(juce::ToggleButton& button, const char* paramId, const juce::String& tooltip);
    void setupBar(juce::Slider& slider, const char* paramId, const juce::String& caption, const juce::String& tooltip);
    void selectEnvelope(int index);

    OpenGlitchFXProcessor& processor_;
    const GlitchLookAndFeel& lnf_;

    std::array<Card, 4> cards_;
    std::vector<std::pair<juce::Component*, juce::String>> captions_;

    juce::ComboBox stutterSyncBox_;
    juce::ComboBox stutterReverseBox_;
    juce::ToggleButton stutterAdvanceButton_{ "ADVANCE" };
    juce::Slider stutterGateSlider_;
    juce::Slider stutterPitchSlider_;

    juce::ComboBox tapeDirectionBox_;
    juce::ComboBox tapeCurveBox_;
    juce::ComboBox tapeLengthBox_;
    juce::ToggleButton tapeSlipButton_{ "SLIP" };
    juce::ToggleButton tapeLowpassButton_{ "LPF" };
    juce::ToggleButton tapeFadeButton_{ "FADE" };

    std::array<juce::TextButton, 4> envTabs_;
    EnvelopeEditorComponent envEditor_;
    juce::ToggleButton envOnButton_{ "ON" };
    juce::ComboBox envRateBox_;
    juce::Slider envDepthSlider_;
    juce::TextButton envResetButton_{ "RESET" };
    std::unique_ptr<ButtonAttachment> envOnAttach_;
    std::unique_ptr<ComboBoxAttachment> envRateAttach_;
    std::unique_ptr<SliderAttachment> envDepthAttach_;
    int selectedEnv_ = 0;

    juce::ComboBox filterModeBox_;
    juce::Slider filterCutoffSlider_;
    juce::Slider filterResoSlider_;
    juce::ComboBox triggerModeBox_;
    juce::ComboBox triggerStartBox_;
    juce::ComboBox mixModeBox_;

    // Declared after the controls, so they are destroyed first
    std::vector<std::unique_ptr<ComboBoxAttachment>> comboAttachments_;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments_;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DetailPanelComponent)
};

} // namespace openglitch::ui
