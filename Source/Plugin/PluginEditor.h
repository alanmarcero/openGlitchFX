#pragma once

#include "DetailPanelComponent.h"
#include "GlitchLookAndFeel.h"
#include "ParamLockHelper.h"
#include "PluginProcessor.h"
#include "StepSequencerComponent.h"
#include "../suite/SuiteBar.h"
#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

// The whole editor at its design size. OpenGlitchFXEditor scales it.
class EditorContent final : public juce::Component,
                            public juce::TooltipClient,
                            private juce::Timer {
public:
    static constexpr int kDesignWidth = 1440;
    static constexpr int kDesignHeight = 1120;
    static constexpr float kMinScale = 0.7f;
    static constexpr float kMaxScale = 1.5f;

    explicit EditorContent(OpenGlitchFXProcessor& owner);
    ~EditorContent() override;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;
    juce::String getTooltip() override;

    static juce::String stepPillText(int currentStep, int loopLength, bool gateOpen, bool midiMode);
    juce::PopupMenu createRandomizeMenu();
    // Public so a test can run one refresh without a message loop.
    void timerCallback() override;
    bool isClipLatched() const noexcept { return clipHold_ > 0.1f; }
    float meterPeakHold(int index) const noexcept {
        return (index >= 0 && index < 3) ? meterPeaks_[static_cast<size_t>(index)].value : 0.0f;
    }
    juce::Rectangle<float> meterBounds() const noexcept { return masterMeterBounds_; }
    void resetMeters() noexcept {
        clipHold_ = 0.0f;
        for (auto& peak : meterPeaks_) {
            peak.reset();
        }
    }

private:
    struct MeterPeakHold {
        static constexpr int kHoldTicks = 30;
        float value = 0.0f;
        int holdTicks = 0;

        void update(float level) noexcept {
            if (level >= value) {
                value = level;
                holdTicks = kHoldTicks;
                return;
            }
            if (holdTicks > 0) {
                --holdTicks;
                return;
            }
            value = std::max(level, value * 0.85f);
        }

        void reset() noexcept {
            value = 0.0f;
            holdTicks = 0;
        }
    };

    static float meterNormDb(float peak) noexcept;
    void applyTheme(int themeIndex);
    void showRandomizeMenu();
    int fxCardHeaderAt(juce::Point<float> position) const;

    OpenGlitchFXProcessor& processor_;
    openglitch::ui::GlitchLookAndFeel lookAndFeel_;

    // Embedded Gemini-generated images (Logo & Backdrop Texture)
    std::unique_ptr<juce::Drawable> sliceRegister_;
    juce::Image customLogoImage_;
    juce::Image backdropImage_;

    // Header Controls
    juce::ComboBox themeBox_;
    juce::Label themeLabel_{ {}, "THEME:" };
    juce::Label scaleLabel_{ {}, "SCALE:" };
    juce::ComboBox scaleBox_;
    juce::ToggleButton hostSyncButton_{ "HOST SYNC" };
    juce::Slider internalBpmSlider_;
    juce::Label internalBpmLabel_{ {}, "BPM" };

    // 32-Step Beat Sequencer, Paint Brush Selector & Pattern Quick-Select Buttons
    openglitch::ui::StepSequencerComponent sequencerGrid_;
    juce::Label paintBrushLabel_{ {}, "STEP BRUSH:" };
    juce::ComboBox paintBrushBox_;
    juce::TextButton presetAllBtn_{ "ALL STEPS" };
    juce::TextButton presetLastBeatBtn_{ "LAST BEAT" };
    juce::TextButton presetLast4StepsBtn_{ "LAST 1/8" };
    juce::TextButton presetLast32ndBtn_{ "LAST 1/32" };
    juce::TextButton presetBackbeatBtn_{ "BEATS 2 & 4" };
    juce::TextButton preset16thBtn_{ "16TH OFFBEATS" };
    juce::TextButton presetInvertBtn_{ "INVERT" };
    juce::TextButton presetResetFxBtn_{ "ALL FX" };
    juce::TextButton presetClearBtn_{ "CLEAR" };
    juce::Label sequenceLengthLabel_{ {}, "LENGTH" };
    juce::Slider sequenceLengthSlider_;
    juce::Label sequenceLengthValueLabel_;
    juce::Label sequencerSwingLabel_{ {}, "SWING" };
    juce::Slider sequencerSwingSlider_;
    juce::Label sequencerSwingValueLabel_;
    juce::Label patternRateLabel_{ {}, "RATE" };
    juce::ComboBox patternRateBox_;
    juce::Label patternChanceLabel_{ {}, "CHANCE" };
    juce::Slider patternChanceSlider_;
    juce::Label patternChanceValueLabel_;
    juce::ToggleButton keepSubButton_{ "KEEP SUB" };
    juce::TextButton randomizeButton_{ "RANDOMIZE" };

    // What a card paints. The timer repaints a card when this changes.
    struct CardLook {
        bool armed = false;
        bool firing = false;
        bool brush = false;
        bool silent = false;
        int pads = 0;
        int envelope = 0;
        bool operator==(const CardLook&) const = default;
    };

    // 15 Glitch FX Slot UI (3 rows x 5 columns)
    struct FxSlotUi {
        juce::String title;
        juce::String subtitle;
        juce::Slider knob;
        juce::ToggleButton enableBtn{ "ON" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAttach;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttach;
        juce::Rectangle<float> cardBounds;
        CardLook look;
    };

    static constexpr int kNumUiFx = openglitch::core::GlitchProcessorCore::kNumEffects;
    std::array<FxSlotUi, kNumUiFx> fxSlots_;

    // Master Output Strip Controls & Live Telemetry Meter
    juce::Slider smoothSlider_;
    juce::Label smoothLabel_{ {}, "DE-CLICK" };
    juce::Slider masterMixSlider_;
    juce::Label masterMixLabel_{ {}, "WET MIX" };
    juce::Slider masterGainSlider_;
    juce::Label masterGainLabel_{ {}, "OUT GAIN" };
    juce::ToggleButton masterPowerButton_{ "MASTER ON" };
    juce::Rectangle<float> masterCardBounds_;
    juce::Rectangle<float> masterMeterBounds_;
    float clipHold_ = 0.0f;
    std::array<MeterPeakHold, 3> meterPeaks_{};

    openglitch::ui::DetailPanelComponent detailPanel_;
    int lastStateVersion_ = -1;
    int lastLockVersion_ = -1;
    int hoveredFxHeader_ = -1;
    juce::TooltipWindow tooltipWindow_{ this, 500 };
    std::unique_ptr<openglitch::suite::SuiteBar> suiteBar_;
    juce::String suiteToken_;

    // Attachments
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<ComboBoxAttachment> themeAttach_;
    std::unique_ptr<ButtonAttachment> hostSyncAttach_;
    std::unique_ptr<SliderAttachment> sequenceLengthAttach_;
    std::unique_ptr<SliderAttachment> sequencerSwingAttach_;
    std::unique_ptr<ComboBoxAttachment> patternRateAttach_;
    std::unique_ptr<SliderAttachment> patternChanceAttach_;
    std::unique_ptr<ButtonAttachment> keepSubAttach_;
    std::unique_ptr<SliderAttachment> internalBpmAttach_;
    std::unique_ptr<SliderAttachment> smoothAttach_;
    std::unique_ptr<SliderAttachment> masterMixAttach_;
    std::unique_ptr<SliderAttachment> masterGainAttach_;
    std::unique_ptr<ButtonAttachment> masterPowerAttach_;
    openglitch::ui::ParamLockOverlayHelper lockHelper_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorContent)
};

class OpenGlitchFXEditor final : public juce::AudioProcessorEditor {
public:
    explicit OpenGlitchFXEditor(OpenGlitchFXProcessor& owner);

    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    OpenGlitchFXProcessor& processor_;
    EditorContent content_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenGlitchFXEditor)
};
