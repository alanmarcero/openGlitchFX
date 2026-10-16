#pragma once

#include "GlitchLookAndFeel.h"
#include "PluginProcessor.h"
#include "StepSequencerComponent.h"
#include <array>
#include <juce_gui_basics/juce_gui_basics.h>

class OpenGlitchFXEditor final : public juce::AudioProcessorEditor,
                                 private juce::Timer,
                                 private juce::AudioProcessorValueTreeState::Listener {
public:
    explicit OpenGlitchFXEditor(OpenGlitchFXProcessor& owner);
    ~OpenGlitchFXEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void applyTheme(int themeIndex);

    OpenGlitchFXProcessor& processor_;
    openglitch::ui::GlitchLookAndFeel lookAndFeel_;

    // Embedded Gemini-generated images (Logo & Backdrop Texture)
    juce::Image customLogoImage_;
    juce::Image backdropImage_;

    // Header Controls
    juce::ComboBox themeBox_;
    juce::Label themeLabel_{ {}, "LOOK:" };
    juce::ToggleButton hostSyncButton_{ "HOST SYNC" };
    juce::Slider internalBpmSlider_;
    juce::Label internalBpmLabel_{ {}, "BPM" };

    // 32-Step Beat Sequencer & Pattern Quick-Select Buttons
    openglitch::ui::StepSequencerComponent sequencerGrid_;
    juce::TextButton presetAllBtn_{ "ALL 32" };
    juce::TextButton presetLastBeatBtn_{ "LAST BEAT (25-32)" };
    juce::TextButton presetLast4StepsBtn_{ "LAST 4 STEPS" };
    juce::TextButton presetLast32ndBtn_{ "LAST 1/32" };
    juce::TextButton presetBackbeatBtn_{ "BEATS 2 & 4" };
    juce::TextButton preset16thBtn_{ "1/16 ALT" };
    juce::TextButton presetInvertBtn_{ "INVERT" };
    juce::TextButton presetClearBtn_{ "CLEAR" };

    // 10 Glitch FX Slot UI (2 rows x 5 columns)
    struct FxSlotUi {
        juce::String title;
        juce::String subtitle;
        juce::Slider knob;
        juce::ToggleButton enableBtn{ "ON" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAttach;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttach;
        juce::Rectangle<float> cardBounds;
    };

    static constexpr int kNumUiFx = 10;
    std::array<FxSlotUi, kNumUiFx> fxSlots_;

    // Master Output Strip Controls
    juce::Slider smoothSlider_;
    juce::Label smoothLabel_{ {}, "DE-CLICK" };
    juce::Slider masterMixSlider_;
    juce::Label masterMixLabel_{ {}, "WET MIX" };
    juce::Slider masterGainSlider_;
    juce::Label masterGainLabel_{ {}, "OUT GAIN" };
    juce::ToggleButton masterPowerButton_{ "MASTER ON" };
    juce::Rectangle<float> masterCardBounds_;

    // Attachments
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<ComboBoxAttachment> themeAttach_;
    std::unique_ptr<ButtonAttachment> hostSyncAttach_;
    std::unique_ptr<SliderAttachment> internalBpmAttach_;
    std::unique_ptr<SliderAttachment> smoothAttach_;
    std::unique_ptr<SliderAttachment> masterMixAttach_;
    std::unique_ptr<SliderAttachment> masterGainAttach_;
    std::unique_ptr<ButtonAttachment> masterPowerAttach_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenGlitchFXEditor)
};
