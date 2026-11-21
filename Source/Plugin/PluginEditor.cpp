#include "PluginEditor.h"
#include "BinaryData.h"
#include <ranges>

namespace {
// Steps of the card envelope bar that cause a repaint.
constexpr float kEnvelopeSteps = 64.0f;
} // namespace

EditorContent::EditorContent(OpenGlitchFXProcessor& owner)
    : processor_(owner),
      sequencerGrid_(owner.parameters, lookAndFeel_, owner.getUndo()),
      detailPanel_(owner, lookAndFeel_) {
    setLookAndFeel(&lookAndFeel_);

    customLogoImage_ = juce::ImageCache::getFromMemory(BinaryData::alan_marcero_logo_jpg,
                                                       BinaryData::alan_marcero_logo_jpgSize);
    backdropImage_ = juce::ImageCache::getFromMemory(BinaryData::analogcyberfaceplate_png,
                                                     BinaryData::analogcyberfaceplate_pngSize);

    sliceRegister_ = juce::Drawable::createFromImageData(BinaryData::sliceregister_svg, BinaryData::sliceregister_svgSize);

    // Header Look Selector
    themeBox_.addItemList({ "Cyber Neon", "Industrial Amber", "Acid Matrix", "Arctic Glitch" }, 1);
    themeBox_.setTitle("Theme");
    themeBox_.setTooltip("Choose a dark colour scheme for the rack, menus and envelopes.");
    themeBox_.setJustificationType(juce::Justification::centred);
    themeBox_.onChange = [this]() { applyTheme(themeBox_.getSelectedItemIndex()); };
    addAndMakeVisible(themeBox_);

    scaleBox_.addItemList({ "70%", "85%", "100%", "125%" }, 1);
    scaleBox_.setTitle("Editor Scale");
    scaleBox_.setTooltip("Editor size. Drag the bottom-right corner for any size from 70% to 150%.");
    scaleBox_.setJustificationType(juce::Justification::centred);
    scaleBox_.onChange = [this]() {
        const float scale = scaleBox_.getText().getFloatValue() / 100.0f;
        if (auto* editor = getParentComponent()) {
            editor->setSize(juce::roundToInt(kDesignWidth * scale), juce::roundToInt(kDesignHeight * scale));
        }
    };
    addAndMakeVisible(scaleBox_);

    themeLabel_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    themeLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(themeLabel_);

    hostSyncButton_.setTitle("Host Sync");
    hostSyncButton_.setTooltip("Follow the host's tempo and bar position. Turn off to run on the internal BPM.");
    addAndMakeVisible(hostSyncButton_);

    internalBpmSlider_.setSliderStyle(juce::Slider::LinearBar);
    internalBpmSlider_.setTitle("Internal BPM");
    internalBpmSlider_.setTooltip("Internal tempo. Used when HOST SYNC is off, or when the host reports no tempo.");
    addAndMakeVisible(internalBpmSlider_);

    // 32-Step Beat Sequencer Grid, Paint-Brush FX Selector & Quick Pattern Buttons
    addAndMakeVisible(sequencerGrid_);

    paintBrushLabel_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    paintBrushLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(paintBrushLabel_);

    paintBrushBox_.addItem("ALL ACTIVE FX (DEFAULT)", 1);
    for (int fx = 0; fx < kNumUiFx; ++fx) {
        const auto& desc = openglitch::params::kFxDescriptors[static_cast<size_t>(fx)];
        paintBrushBox_.addItem("ONLY: " + juce::String(desc.name) + " [" + desc.shortCode + "]", fx + 2);
    }
    paintBrushBox_.setSelectedId(1, juce::dontSendNotification);
    paintBrushBox_.setTitle("Step Brush");
    paintBrushBox_.setTooltip("What a left-click on a pad paints. You can also click an FX card's title to pick it.");
    paintBrushBox_.onChange = [this]() {
        const int idx = paintBrushBox_.getSelectedItemIndex(); // 0 = All (-1), 1..15 = FX 0..14
        sequencerGrid_.setPaintTargetFx(idx - 1);
    };
    addAndMakeVisible(paintBrushBox_);

    auto setupPresetBtn = [this](juce::TextButton& btn, const juce::String& tooltip, std::function<void()> fn) {
        btn.onClick = std::move(fn);
        btn.setTitle(btn.getButtonText());
        btn.setTooltip(tooltip);
        addAndMakeVisible(btn);
    };

    using openglitch::core::StepPatternPreset;
    auto editPattern = [this](const juce::String& name, auto action) {
        return [this, name, edit = std::move(action)] {
            processor_.getUndo().perform(name, edit);
        };
    };
    setupPresetBtn(presetAllBtn_,        "Turn on all 32 pads. Uses the pad brush FX if one is selected.",
                   editPattern("Pattern", [this] { sequencerGrid_.applyPreset(StepPatternPreset::AllOn); }));
    setupPresetBtn(presetLastBeatBtn_,   "Pads 25-32: beat 4 at RATE 1x. Uses the pad brush FX if one is selected.",
                   editPattern("Pattern", [this] { sequencerGrid_.applyPreset(StepPatternPreset::LastBeat); }));
    setupPresetBtn(presetLast4StepsBtn_, "Pads 29-32: the last 1/8 note at RATE 1x. Uses the pad brush FX if one is selected.",
                   editPattern("Pattern", [this] { sequencerGrid_.applyPreset(StepPatternPreset::LastFourSteps); }));
    setupPresetBtn(presetLast32ndBtn_,   "Pad 32 only: the last 1/32 note at RATE 1x. Uses the pad brush FX if one is selected.",
                   editPattern("Pattern", [this] { sequencerGrid_.applyPreset(StepPatternPreset::Last32nd); }));
    setupPresetBtn(presetBackbeatBtn_,   "Pads 9-16 and 25-32: beats 2 and 4 at RATE 1x. Uses the pad brush FX if one is selected.",
                   editPattern("Pattern", [this] { sequencerGrid_.applyPreset(StepPatternPreset::Backbeat2And4); }));
    setupPresetBtn(preset16thBtn_,       "The second 1/16 note of every 1/8 note at RATE 1x. Uses the pad brush FX if one is selected.",
                   editPattern("Pattern", [this] { sequencerGrid_.applyPreset(StepPatternPreset::Stutter16ths); }));
    setupPresetBtn(presetInvertBtn_,     "Swap on and off for every pad.",
                   editPattern("Pattern", [this] { sequencerGrid_.invertPattern(); }));
    setupPresetBtn(presetResetFxBtn_,    "Route all 15 FX to every pad. Pad on/off stays the same.",
                   editPattern("Pattern", [this] { sequencerGrid_.resetAllStepFxMasksToAll(); }));
    setupPresetBtn(presetClearBtn_,      "Turn off all 32 pads.",
                   editPattern("Pattern", [this] { sequencerGrid_.applyPreset(StepPatternPreset::ClearAll); }));

    auto setupSequencerControl = [this](juce::Label& label, juce::Slider& slider) {
        label.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        label.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(label);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setMouseDragSensitivity(180);
        addAndMakeVisible(slider);
    };
    sequenceLengthSlider_.setRange(1.0, 32.0, 1.0);
    sequenceLengthSlider_.setTitle("Sequence Length");
    sequenceLengthSlider_.setTooltip("Number of steps before the pattern loops. Pads after it do not play.");
    sequenceLengthValueLabel_.setJustificationType(juce::Justification::centredLeft);
    sequenceLengthValueLabel_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    addAndMakeVisible(sequenceLengthValueLabel_);
    setupSequencerControl(sequenceLengthLabel_, sequenceLengthSlider_);

    sequencerSwingSlider_.setRange(0.0, 100.0, 0.1);
    sequencerSwingSlider_.setTitle("Sequencer Swing");
    sequencerSwingSlider_.setTooltip("Delay odd-numbered steps in each pair for a swung glitch rhythm.");
    sequencerSwingValueLabel_.setJustificationType(juce::Justification::centredLeft);
    sequencerSwingValueLabel_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    addAndMakeVisible(sequencerSwingValueLabel_);
    setupSequencerControl(sequencerSwingLabel_, sequencerSwingSlider_);

    patternRateLabel_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    patternRateLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(patternRateLabel_);
    patternRateBox_.addItemList({ "1/2x", "1x", "2x" }, 1);
    patternRateBox_.setTitle("Pattern Rate");
    patternRateBox_.setTooltip("Global sequencer speed multiplier relative to host tempo (1/2x, 1x, or 2x).");
    addAndMakeVisible(patternRateBox_);

    patternChanceSlider_.setRange(0.0, 100.0, 1.0);
    patternChanceSlider_.setTitle("Pattern Chance");
    patternChanceSlider_.setTooltip("Deterministic step probability gate across bars (100% = every bar fires).");
    patternChanceValueLabel_.setJustificationType(juce::Justification::centredLeft);
    patternChanceValueLabel_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    addAndMakeVisible(patternChanceValueLabel_);
    setupSequencerControl(patternChanceLabel_, patternChanceSlider_);

    keepSubButton_.setTitle("Keep Sub");
    keepSubButton_.setTooltip("Preserve clean sub-bass below 95 Hz while glitching mids and highs.");
    addAndMakeVisible(keepSubButton_);

    randomizeButton_.setTitle("Randomize");
    randomizeButton_.setTooltip("Create a repeatable-density pattern. With an FX brush selected, randomize only that FX routing.");
    randomizeButton_.onClick = [this]() { showRandomizeMenu(); };
    addAndMakeVisible(randomizeButton_);

    // 15 Glitch FX Slots (3 Rows x 5 Columns)
    using namespace openglitch::params;
    struct FxMeta {
        const char* title;
        const char* sub;
        const char* amtId;
        const char* onId;
        double defaultPct;
    };

    const std::array<FxMeta, kNumUiFx> metas = {{
        // Row 1: Initial 5 Glitch FX
        { "BITCRUSH [BC]",     "BITRATE & DEPTH",  kBitcrushAmt,    kBitcrushOn,    35.0 },
        { "DOWNSAMPLE [DS]",   "SAMPLE & HOLD",    kDownsampleAmt,  kDownsampleOn,  0.0  },
        { "STUTTER [ST]",      "BEAT REPEAT",      kStutterAmt,     kStutterOn,     0.0  },
        { "TAPE STOP [TS]",    "SLOW TO STOP",     kTapeStopAmt,    kTapeStopOn,    0.0  },
        { "RING MOD [RM]",     "SINE CARRIER",     kRingModAmt,     kRingModOn,     0.0  },
        // Row 2: 5 Additional Glitch FX (10 Total)
        { "REVERSE [RV]",      "FULL SPAN REVERSE",  kReverseAmt,     kReverseOn,     0.0  },
        { "CHOPPER [CH]",      "RHYTHMIC SLICER",  kChopperAmt,     kChopperOn,     0.0  },
        { "PITCH GLITCH [PG]", "OCTAVE ARPEGGIO",  kPitchGlitchAmt, kPitchGlitchOn, 0.0  },
        { "COMB RESO [CR]",    "SHORT COMB",       kCombResoAmt,    kCombResoOn,    0.0  },
        { "WAVEFOLD [WF]",     "HARMONIC FOLD",    kWavefoldAmt,    kWavefoldOn,    0.0  },
        { "CHORUS [CO]", "STEREO ENSEMBLE", kChorusAmt, kChorusOn, 0.0 },
        { "FLANGER [FL]", "FEEDBACK SWEEP", kFlangerAmt, kFlangerOn, 0.0 },
        { "PHASER [PH]", "6 ALLPASS STAGES", kPhaserAmt, kPhaserOn, 0.0 },
        { "PING-PONG [PP]", "SYNCED ECHO", kPingPongAmt, kPingPongOn, 0.0 },
        { "SATURATION [SA]", "TANH DRIVE", kSaturationAmt, kSaturationOn, 0.0 }
    }};

    for (size_t i = 0; i < kNumUiFx; ++i) {
        auto& slot = fxSlots_[i];
        slot.title = metas[i].title;
        slot.subtitle = metas[i].sub;

        slot.knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slot.knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
        addAndMakeVisible(slot.knob);

        addAndMakeVisible(slot.enableBtn);
        const auto fxName = juce::String(metas[i].title).upToFirstOccurrenceOf(" [", false, false);
        slot.knob.setTitle(fxName + " Amount");
        slot.enableBtn.setTitle(fxName + " Enable");
        slot.knob.setTooltip(fxName + " amount. At 0% this FX does nothing, even on pads routed to it.");
        slot.enableBtn.setTooltip("Turn this FX module on or off for every pad.");

        slot.knobAttach = std::make_unique<SliderAttachment>(processor_.parameters, metas[i].amtId, slot.knob);
        slot.enableAttach = std::make_unique<ButtonAttachment>(processor_.parameters, metas[i].onId, slot.enableBtn);
        slot.knob.setDoubleClickReturnValue(true, metas[i].defaultPct);
    }

    // Master Controls
    auto setupMasterRotary = [this](juce::Slider& s, juce::Label& l, const juce::String& title) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 15);
        s.setTitle(title);
        addAndMakeVisible(s);

        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        addAndMakeVisible(l);
    };

    setupMasterRotary(smoothSlider_,     smoothLabel_,     "De-Click");
    setupMasterRotary(masterMixSlider_,  masterMixLabel_,  "Wet Mix");
    setupMasterRotary(masterGainSlider_, masterGainLabel_, "Out Gain");
    masterPowerButton_.setTitle("Master Power");
    addAndMakeVisible(masterPowerButton_);
    smoothSlider_.setTooltip("Fade time when a pad turns on or off. Raise it if pad edges click.");
    masterMixSlider_.setTooltip("Dry/wet balance on active pads. See DRY/WET mode below.");
    masterGainSlider_.setTooltip("Output gain.");
    masterPowerButton_.setTooltip("Turn off to bypass every effect.");

    addAndMakeVisible(detailPanel_);

    themeAttach_       = std::make_unique<ComboBoxAttachment>(processor_.parameters, kTheme, themeBox_);
    hostSyncAttach_    = std::make_unique<ButtonAttachment>(processor_.parameters, kHostSync, hostSyncButton_);
    sequenceLengthAttach_ = std::make_unique<SliderAttachment>(processor_.parameters, kSequenceLength, sequenceLengthSlider_);
    sequencerSwingAttach_ = std::make_unique<SliderAttachment>(processor_.parameters, kSequencerSwing, sequencerSwingSlider_);
    patternRateAttach_    = std::make_unique<ComboBoxAttachment>(processor_.parameters, kPatternRate, patternRateBox_);
    patternChanceAttach_  = std::make_unique<SliderAttachment>(processor_.parameters, kPatternChance, patternChanceSlider_);
    keepSubAttach_        = std::make_unique<ButtonAttachment>(processor_.parameters, kKeepSub, keepSubButton_);
    internalBpmAttach_ = std::make_unique<SliderAttachment>(processor_.parameters, kInternalBpm, internalBpmSlider_);
    smoothAttach_      = std::make_unique<SliderAttachment>(processor_.parameters, kSmoothMs, smoothSlider_);
    masterMixAttach_   = std::make_unique<SliderAttachment>(processor_.parameters, kMasterMix, masterMixSlider_);
    masterGainAttach_  = std::make_unique<SliderAttachment>(processor_.parameters, kMasterGain, masterGainSlider_);
    masterPowerAttach_ = std::make_unique<ButtonAttachment>(processor_.parameters, kMasterPower, masterPowerButton_);

    internalBpmSlider_.setDoubleClickReturnValue(true, 120.0);
    smoothSlider_.setDoubleClickReturnValue(true, 2.5);
    masterMixSlider_.setDoubleClickReturnValue(true, 100.0);
    masterGainSlider_.setDoubleClickReturnValue(true, 0.0);
    sequenceLengthValueLabel_.setText(juce::String(static_cast<int>(std::round(sequenceLengthSlider_.getValue()))) + " STEPS",
                                      juce::dontSendNotification);
    sequencerSwingValueLabel_.setText(juce::String(sequencerSwingSlider_.getValue(), 0) + "%",
                                      juce::dontSendNotification);
    patternChanceValueLabel_.setText(juce::String(patternChanceSlider_.getValue(), 0) + "%",
                                     juce::dontSendNotification);
    patternChanceValueLabel_.setText(juce::String(patternChanceSlider_.getValue(), 0) + "%",
                                     juce::dontSendNotification);
    patternChanceSlider_.setDoubleClickReturnValue(true, 100.0);

    applyTheme(themeBox_.getSelectedItemIndex());

    openglitch::suite::SuiteBar::Hooks hooks;
    hooks.undo = [this] { processor_.getUndo().undo(); };
    hooks.redo = [this] { processor_.getUndo().redo(); };
    hooks.canUndo = [this] { return processor_.getUndo().canUndo(); };
    hooks.canRedo = [this] { return processor_.getUndo().canRedo(); };
    hooks.abIsA = [this] { return processor_.abIsA(); };
    hooks.selectA = [this] { processor_.selectAbSlot(true); };
    hooks.selectB = [this] { processor_.selectAbSlot(false); };
    hooks.copyAtoB = [this] { processor_.copyAbAtoB(); };
    hooks.copyBtoA = [this] { processor_.copyAbBtoA(); };
    hooks.loadFactory = [this](int index) { processor_.loadFactoryProgram(index); };
    hooks.loadUser = [this](const juce::String& name) { processor_.loadUserPreset(name); };
    hooks.saveUser = [this](const juce::String& name, juce::String& error) { return processor_.saveUserPreset(name, error); };
    hooks.renameUser = [this](const juce::String& from, const juce::String& to, juce::String& error) {
        return processor_.renameUserPreset(from, to, error);
    };
    hooks.deleteUser = [this](const juce::String& name, juce::String& error) { return processor_.deleteUserPreset(name, error); };
    hooks.currentLabel = [this] { return processor_.currentPresetLabel(); };
    hooks.currentIsFactory = [this] { return processor_.currentPresetIsFactory(); };
    hooks.currentIsEdited = [this] { return processor_.currentPresetIsEdited(); };
    hooks.currentFactoryIndex = [this] { return processor_.getCurrentProgram(); };
    suiteBar_ = std::make_unique<openglitch::suite::SuiteBar>(processor_.getPresets(), std::move(hooks));
    addAndMakeVisible(*suiteBar_);

    setWantsKeyboardFocus(true);
    setSize(kDesignWidth, kDesignHeight);
    timerCallback();
    startTimerHz(30);
}

EditorContent::~EditorContent() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void EditorContent::applyTheme(int themeIndex) {
    themeIndex = std::clamp(themeIndex, 0, 3);
    lookAndFeel_.setTheme(static_cast<openglitch::ui::Theme>(themeIndex));
    const auto& pal = lookAndFeel_.getPalette();

    themeLabel_.setColour(juce::Label::textColourId, pal.textMuted);
    paintBrushLabel_.setColour(juce::Label::textColourId, pal.accentPrimary);
    smoothLabel_.setColour(juce::Label::textColourId, pal.textMuted);
    masterMixLabel_.setColour(juce::Label::textColourId, pal.textMuted);
    masterGainLabel_.setColour(juce::Label::textColourId, pal.textMuted);
    sequenceLengthLabel_.setColour(juce::Label::textColourId, pal.textMuted);
    sequenceLengthValueLabel_.setColour(juce::Label::textColourId, pal.accentPrimary);
    sequencerSwingLabel_.setColour(juce::Label::textColourId, pal.textMuted);
    sequencerSwingValueLabel_.setColour(juce::Label::textColourId, pal.accentPrimary);
    patternRateLabel_.setColour(juce::Label::textColourId, pal.textMuted);
    patternChanceLabel_.setColour(juce::Label::textColourId, pal.textMuted);
    patternChanceValueLabel_.setColour(juce::Label::textColourId, pal.accentPrimary);
    sequenceLengthSlider_.setColour(juce::Slider::thumbColourId, pal.accentPrimary);
    sequenceLengthSlider_.setColour(juce::Slider::trackColourId, pal.accentPrimary);
    sequencerSwingSlider_.setColour(juce::Slider::thumbColourId, pal.accentSecondary);
    sequencerSwingSlider_.setColour(juce::Slider::trackColourId, pal.accentSecondary);
    patternChanceSlider_.setColour(juce::Slider::thumbColourId, pal.accentPrimary);
    patternChanceSlider_.setColour(juce::Slider::trackColourId, pal.accentPrimary);

    sendLookAndFeelChange();
    repaint();
}

juce::PopupMenu EditorContent::createRandomizeMenu() {
    juce::PopupMenu menu;
    menu.setLookAndFeel(&lookAndFeel_);
    menu.addSectionHeader(sequencerGrid_.getPaintTargetFx() >= 0 ? "Randomize selected FX lane" : "Randomize the full pattern");
    menu.addItem(1, "Light (35% step density)");
    menu.addItem(2, "Medium (55% step density)");
    menu.addItem(3, "Wild (75% step density)");
    return menu;
}

void EditorContent::showRandomizeMenu() {
    createRandomizeMenu().showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(&randomizeButton_),
                       [safeThis = juce::Component::SafePointer<EditorContent>(this)](int choice) {
        if (safeThis != nullptr && choice > 0) {
            const auto seed = static_cast<uint32_t>(juce::Random::getSystemRandom().nextInt());
            safeThis->processor_.getUndo().perform("Randomize", [&] {
                safeThis->sequencerGrid_.randomizePattern(seed, choice);
            });
        }
    });
}

int EditorContent::fxCardHeaderAt(juce::Point<float> position) const {
    const auto hit = std::ranges::find_if(fxSlots_, [&](const FxSlotUi& slot) {
        const auto& r = slot.cardBounds;
        return juce::Rectangle<float>(r.getX(), r.getY(), r.getWidth(), 36.0f).contains(position);
    });
    return hit == fxSlots_.end() ? -1 : static_cast<int>(std::distance(fxSlots_.begin(), hit));
}

void EditorContent::mouseDown(const juce::MouseEvent& event) {
    if (masterMeterBounds_.contains(event.position)) {
        clipHold_ = 0.0f;
        repaint(masterMeterBounds_.expanded(4.0f).toNearestInt());
        return;
    }
    // Clicking an FX card's top header plate toggles/selects that FX as the active Pad Brush!
    const int fx = fxCardHeaderAt(event.position);
    if (fx < 0) {
        return;
    }
    const int targetBrush = (sequencerGrid_.getPaintTargetFx() == fx) ? -1 : fx;
    paintBrushBox_.setSelectedId(targetBrush + 2, juce::sendNotificationSync);
}

juce::String EditorContent::getTooltip() {
    const int fx = fxCardHeaderAt(getMouseXYRelative().toFloat());
    if (fx < 0) {
        return {};
    }
    const int pads = sequencerGrid_.countStepsRoutingToFx(fx);
    return juce::String("Click to ") + (sequencerGrid_.getPaintTargetFx() == fx ? "clear" : "select")
         + " this FX as the pad brush. Active on " + juce::String(pads) + " of " + juce::String(sequencerGrid_.loopLength()) + " pads.";
}

float EditorContent::meterNormDb(float peak) noexcept {
    if (peak <= 1e-5f) return 0.0f;
    const float db = 20.0f * std::log10(peak);
    return std::clamp((db + 36.0f) / 39.0f, 0.0f, 1.0f);
}

void EditorContent::timerCallback() {
    const auto theme = static_cast<int>(processor_.parameters.getRawParameterValue(openglitch::params::kTheme)->load());
    if (theme != static_cast<int>(lookAndFeel_.getTheme())) applyTheme(theme);
    const auto tel = processor_.getTelemetry();
    if (tel.clipOut || tel.peakL >= 1.0f || tel.peakR >= 1.0f) {
        clipHold_ = 1.0f;
    }
    meterPeaks_[0].update(meterNormDb(std::max(tel.inputPeakL, tel.inputPeakR)));
    meterPeaks_[1].update(meterNormDb(tel.peakL));
    meterPeaks_[2].update(meterNormDb(tel.peakR));
    sequencerGrid_.setPlayheadState(tel.currentStep, tel.barPhase, tel.stepEnabled, tel.activeGate);
    sequencerGrid_.refreshPattern();

    const bool gateOpen = tel.activeGate > 0.25f;
    std::ranges::for_each(std::views::iota(0, kNumUiFx), [&](int fx) {
        auto& slot = fxSlots_[static_cast<size_t>(fx)];
        const bool on = slot.enableBtn.getToggleState();
        const bool audible = slot.knob.getValue() > 0.1;
        const CardLook look{ on && audible,
                             on && audible && gateOpen && ((tel.currentStepFxMask >> fx) & 1u) != 0u,
                             sequencerGrid_.getPaintTargetFx() == fx,
                             on && !audible,
                             sequencerGrid_.countStepsRoutingToFx(fx),
                             juce::roundToInt(std::clamp(tel.fxEnvelopes[static_cast<size_t>(fx)], 0.0f, 1.0f) * kEnvelopeSteps) };
        if (look != slot.look) {
            slot.look = look;
            repaint(slot.cardBounds.expanded(4.0f).toNearestInt());
        }
    });

    using namespace openglitch::params;
    sequencerGrid_.setMidiTriggerMode(processor_.parameters.getRawParameterValue(kTriggerMode)->load() > 0.5f);
    internalBpmSlider_.setAlpha(hostSyncButton_.getToggleState() ? 0.45f : 1.0f);
    sequenceLengthValueLabel_.setText(juce::String(static_cast<int>(std::round(sequenceLengthSlider_.getValue()))) + " STEPS",
                                      juce::dontSendNotification);
    sequencerSwingValueLabel_.setText(juce::String(sequencerSwingSlider_.getValue(), 0) + "%",
                                      juce::dontSendNotification);
    patternChanceValueLabel_.setText(juce::String(patternChanceSlider_.getValue(), 0) + "%",
                                     juce::dontSendNotification);
    const int paintTargetFx = sequencerGrid_.getPaintTargetFx();
    if (paintTargetFx >= 0 && paintTargetFx < kNumUiFx) {
        const auto& desc = openglitch::params::kFxDescriptors[static_cast<size_t>(paintTargetFx)];
        randomizeButton_.setTooltip(juce::String("Randomize only ") + desc.name + " routing across pads. Existing pad on/off states and other FX routes stay unchanged.");
    } else {
        randomizeButton_.setTooltip("Create a full pattern. Higher levels use denser pads and a wider range of FX routes.");
    }

    if (const int version = processor_.getStateVersion(); version != lastStateVersion_) {
        lastStateVersion_ = version;
        detailPanel_.refreshFromState();
    }
    detailPanel_.setTelemetry(tel);
    detailPanel_.refreshDependencies();
    const auto scaleText = juce::String(juce::roundToInt(processor_.getEditorScale() * 100.0f)) + "%";
    if (scaleBox_.getText() != scaleText) {
        scaleBox_.setText(scaleText, juce::dontSendNotification);
    }
    if (suiteBar_ != nullptr) {
        const auto token = juce::String(processor_.abIsA() ? 1 : 0)
            + processor_.currentPresetLabel()
            + juce::String(processor_.getUndo().canUndo() ? 1 : 0)
            + juce::String(processor_.getUndo().canRedo() ? 1 : 0)
            + juce::String(processor_.currentPresetIsFactory() ? 1 : 0)
            + juce::String(processor_.currentPresetIsEdited() ? 1 : 0);
        if (token != suiteToken_) {
            suiteToken_ = token;
            suiteBar_->refresh();
        }
    }
    repaint(masterMeterBounds_.expanded(4.0f).toNearestInt());
    repaint(240, 12, 140, 34);
    repaint(385, 6, 144, 46);
}

void EditorContent::paint(juce::Graphics& g) {
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    const auto& pal = lookAndFeel_.getPalette();

    juce::ColourGradient bgGrad(pal.bgTop, 0.0f, 0.0f, pal.bgBottom, 0.0f, h, false);
    g.setGradientFill(bgGrad);
    g.fillRect(0.0f, 0.0f, w, h);

    if (backdropImage_.isValid()) {
        g.setOpacity(0.12f);
        g.drawImage(backdropImage_, juce::Rectangle<float>(0.0f, 0.0f, w, h),
                    juce::RectanglePlacement::stretchToFit);
        g.setOpacity(1.0f);
    }

    // Outer Chassis Frame & Corner Rack Screws
    g.setColour(pal.cardBorder);
    g.drawRect(0.0f, 0.0f, w, h, 2.0f);
    lookAndFeel_.drawRackScrew(g, 16.0f, 16.0f, 6.5f);
    lookAndFeel_.drawRackScrew(g, w - 16.0f, 16.0f, 6.5f);
    lookAndFeel_.drawRackScrew(g, 16.0f, h - 16.0f, 6.5f);
    lookAndFeel_.drawRackScrew(g, w - 16.0f, h - 16.0f, 6.5f);

    // 2. Top Brand Header with Custom Alan Marcero Logo
    if (customLogoImage_.isValid()) {
        const juce::Rectangle<float> logoRect(34.0f, 9.0f, 44.0f, 44.0f);
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clipCircle;
        clipCircle.addEllipse(logoRect);
        g.reduceClipRegion(clipCircle);
        g.drawImage(customLogoImage_, logoRect, juce::RectanglePlacement::centred);
        g.setColour(pal.accentPrimary.withAlpha(0.75f));
        g.drawEllipse(logoRect.reduced(0.5f), 1.5f);
    }

    g.setColour(pal.accentPrimary);
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText("ALAN MARCERO", 86, 22, 160, 18, juce::Justification::centredLeft);

    if (sliceRegister_ != nullptr) {
        const juce::Rectangle<float> trace(393.0f, 12.0f, 128.0f, 32.0f);
        g.setColour(pal.bgBottom);
        g.fillRoundedRectangle(trace.expanded(4.0f), 4.0f);
        sliceRegister_->drawWithin(g, trace, juce::RectanglePlacement::centred, 0.48f);
        const auto phase = processor_.getVisualBeat() * 8.0f;
        const int slice = juce::jlimit(0, 7, static_cast<int>(phase));
        const auto cell = trace.withTrimmedLeft(16.0f * static_cast<float>(slice)).withWidth(16.0f);
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(cell.toNearestInt());
        g.setColour(pal.accentPrimary.withAlpha(0.23f));
        g.fillRect(cell);
        const float jump = 3.5f * (1.0f - (phase - std::floor(phase)));
        sliceRegister_->drawWithin(g, trace.translated(0.0f, -jump), juce::RectanglePlacement::centred, 1.0f);
    }

    // Center Plugin Title
    g.setColour(pal.textBright);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText("openGlitchFX", static_cast<int>(w * 0.5f - 160.0f), 15, 320, 22, juce::Justification::centred);
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.setColour(pal.accentPrimary.withAlpha(0.75f));
    g.drawText("15-ENGINE RACK  /  32-STEP GLITCH MATRIX", static_cast<int>(w * 0.5f - 160.0f), 38, 320, 14, juce::Justification::centred);

    // Live Gate Status Pill Indicator in Header
    const auto tel = processor_.getTelemetry();
    const bool gateOpen = tel.activeGate > 0.25f;
    const bool midiMode = sequencerGrid_.isMidiTriggerMode();
    juce::Rectangle<float> statusPill(245.0f, 16.0f, 128.0f, 26.0f);
    g.setColour(gateOpen ? pal.accentPrimary.withAlpha(0.25f) : pal.stepInactiveFill);
    g.fillRoundedRectangle(statusPill, 13.0f);
    g.setColour(gateOpen ? pal.accentPrimary : pal.cardBorder);
    g.drawRoundedRectangle(statusPill, 13.0f, 1.3f);
    g.setColour(gateOpen ? pal.textBright : pal.textMuted);
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText(stepPillText(tel.currentStep, sequencerGrid_.loopLength(), gateOpen, midiMode), statusPill.toNearestInt(),
               juce::Justification::centred);

    // Divider line
    g.setColour(pal.cardBorder);
    g.drawLine(28.0f, 58.0f, w - 28.0f, 58.0f, 1.0f);

    // 3. Draw 15 FX Slot Cards (3x5 Matrix) -- highlight when active on the current 1/32 pad!
    for (size_t i = 0; i < kNumUiFx; ++i) {
        const auto& slot = fxSlots_[i];
        const auto& r = slot.cardBounds;
        const bool moduleArmed = slot.look.armed;
        const bool firingNow = slot.look.firing;
        const bool isBrushTarget = slot.look.brush;
        const int padCount = slot.look.pads;
        const float fxEnv = static_cast<float>(slot.look.envelope) / kEnvelopeSteps;

        g.setColour(pal.cardBg);
        g.fillRoundedRectangle(r, 6.0f);

        if (firingNow || isBrushTarget) {
            g.setColour((firingNow ? pal.accentSecondary : pal.accentPrimary).withAlpha(0.1f));
            g.drawRoundedRectangle(r.expanded(1.5f), 7.0f, 2.2f);
        }

        g.setColour(firingNow ? pal.accentSecondary : (isBrushTarget ? pal.stepPlayheadOutline : (moduleArmed ? pal.cardActiveBorder : pal.cardBorder)));
        g.drawRoundedRectangle(r, 6.0f, (firingNow || isBrushTarget) ? 1.8f : (moduleArmed ? 1.4f : 1.0f));

        juce::Rectangle<float> hdr(r.getX() + 6.0f, r.getY() + 5.0f, r.getWidth() - 12.0f, 32.0f);
        g.setColour(firingNow ? pal.accentSecondary.withAlpha(0.28f)
                              : (isBrushTarget ? pal.accentPrimary.withAlpha(0.22f) : pal.headerPlate));
        g.fillRoundedRectangle(hdr, 4.0f);

        // Per-FX Pad Routing Count Badge in top-right corner of header
        juce::Rectangle<float> padBadge(hdr.getRight() - 56.0f, hdr.getY() + 2.0f, 53.0f, 15.0f);
        g.setColour(padCount > 0 ? pal.accentPrimary.withAlpha(0.24f) : juce::Colours::black.withAlpha(0.55f));
        g.fillRoundedRectangle(padBadge, 3.0f);
        g.setColour(padCount > 0 ? pal.accentPrimary : pal.textBright.withAlpha(0.85f));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(juce::String(padCount) + (padCount == 1 ? " PAD" : " PADS"), padBadge.toNearestInt(), juce::Justification::centred);

        g.setColour(firingNow ? pal.textBright : (moduleArmed ? pal.accentPrimary : pal.textBright));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawFittedText(slot.title, hdr.withTrimmedBottom(14.0f).withTrimmedRight(58.0f).withTrimmedLeft(4.0f).toNearestInt(),
                         juce::Justification::centredLeft, 1);

        const bool silent = slot.look.silent;
        const juce::String subtitle = isBrushTarget ? juce::String("PAD BRUSH ACTIVE")
                                                    : (silent ? juce::String("AMOUNT 0%: NO EFFECT") : slot.subtitle);
        g.setColour(isBrushTarget ? pal.stepPlayheadOutline : pal.textMuted);
        g.setFont(juce::FontOptions(11.0f, juce::Font::plain));
        g.drawText(subtitle, hdr.withTrimmedTop(15.0f).withTrimmedLeft(4.0f).toNearestInt(), juce::Justification::centredLeft);

        // Live Per-FX Envelope Activity Bar along bottom of card header
        juce::Rectangle<float> envTrack(hdr.getX() + 2.0f, hdr.getBottom() - 2.2f, hdr.getWidth() - 4.0f, 2.0f);
        g.setColour(pal.stepInactiveFill);
        g.fillRoundedRectangle(envTrack, 1.0f);
        if (fxEnv > 0.01f) {
            g.setColour(firingNow ? pal.accentSecondary : pal.accentPrimary);
            g.fillRoundedRectangle(envTrack.withWidth(envTrack.getWidth() * std::clamp(fxEnv, 0.0f, 1.0f)), 1.0f);
        }
    }

    // 4. Master Card + Live Stereo IN/OUT & Gate Telemetry Bar Meter
    if (!masterCardBounds_.isEmpty()) {
        g.setColour(pal.cardBg);
        g.fillRoundedRectangle(masterCardBounds_, 6.0f);
        g.setColour(pal.cardBorder.brighter(0.2f));
        g.drawRoundedRectangle(masterCardBounds_, 6.0f, 1.2f);

        juce::Rectangle<float> hdr(masterCardBounds_.getX() + 6.0f, masterCardBounds_.getY() + 6.0f,
                                   masterCardBounds_.getWidth() - 12.0f, 22.0f);
        g.setColour(pal.headerPlate);
        g.fillRoundedRectangle(hdr, 4.0f);
        g.setColour(pal.accentPrimary);
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText("MASTER & METERS", hdr.toNearestInt(), juce::Justification::centred);

        if (!masterMeterBounds_.isEmpty()) {
            const auto mb = masterMeterBounds_;
            g.setColour(pal.bgBottom.withAlpha(0.9f));
            g.fillRoundedRectangle(mb, 4.0f);
            g.setColour(pal.cardBorder);
            g.drawRoundedRectangle(mb, 4.0f, 1.0f);

            auto drawBar = [&](float y, const char* label, float val01, juce::Colour col, float peak01 = -1.0f) {
                g.setColour(pal.textMuted);
                g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
                g.drawText(label, static_cast<int>(mb.getX() + 5.0f), static_cast<int>(y - 3.0f), 42, 12, juce::Justification::centredLeft);

                juce::Rectangle<float> track(mb.getX() + 48.0f, y, mb.getWidth() - 54.0f, 6.0f);
                g.setColour(pal.stepInactiveFill);
                g.fillRoundedRectangle(track, 2.0f);

                constexpr int kSegs = 16;
                const float segGap = 1.2f;
                const float segW = (track.getWidth() - segGap * static_cast<float>(kSegs - 1)) / static_cast<float>(kSegs);
                for (int s = 0; s < kSegs; ++s) {
                    const float sn = static_cast<float>(s + 1) / static_cast<float>(kSegs);
                    if (sn <= val01 + 0.03f && val01 > 0.005f) {
                        juce::Colour sc = (s >= 14) ? pal.accentSecondary : col;
                        g.setColour(sc);
                        g.fillRect(track.getX() + static_cast<float>(s) * (segW + segGap), track.getY(), segW, track.getHeight());
                    }
                }
                if (peak01 > 0.005f) {
                    const float px = track.getX() + std::clamp(peak01, 0.0f, 1.0f) * (track.getWidth() - 2.0f);
                    g.setColour(peak01 >= 0.875f ? pal.accentSecondary : pal.textBright);
                    g.fillRect(px, track.getY() - 1.0f, 2.0f, track.getHeight() + 2.0f);
                }
            };

            const float inPeak = std::max(tel.inputPeakL, tel.inputPeakR);
            drawBar(mb.getY() + 6.0f,  "IN",    meterNormDb(inPeak),    pal.accentPrimary.withAlpha(0.85f), meterPeaks_[0].value);
            drawBar(mb.getY() + 20.0f, "OUT L", meterNormDb(tel.peakL), pal.accentPrimary,                   meterPeaks_[1].value);
            drawBar(mb.getY() + 34.0f, "OUT R", meterNormDb(tel.peakR), pal.accentPrimary,                   meterPeaks_[2].value);
            drawBar(mb.getY() + 48.0f, "GATE",  std::clamp(tel.activeGate, 0.0f, 1.0f), pal.accentSecondary);

            // Digital dB & CLIP Indicator along bottom of telemetry box
            const float maxOut = std::max(tel.peakL, tel.peakR);
            const juce::String dbStr = (maxOut <= 1e-4f) ? "OUT: -INF dB"
                                                         : juce::String::formatted("OUT: %+.1f dB", 20.0f * std::log10(maxOut));
            g.setColour(clipHold_ > 0.1f ? pal.accentSecondary : pal.textBright.withAlpha(0.85f));
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText(dbStr, static_cast<int>(mb.getX() + 6.0f), static_cast<int>(mb.getBottom() - 18.0f), 110, 14, juce::Justification::centredLeft);

            juce::Rectangle<float> clipBadge(mb.getRight() - 44.0f, mb.getBottom() - 18.0f, 38.0f, 14.0f);
            g.setColour(clipHold_ > 0.1f ? pal.accentSecondary : pal.stepInactiveFill);
            g.fillRoundedRectangle(clipBadge, 3.0f);
            g.setColour(clipHold_ > 0.1f ? pal.textBright : pal.textMuted);
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText("CLIP", clipBadge.toNearestInt(), juce::Justification::centred);
        }
    }

    g.setColour(pal.textMuted);
    g.setFont(juce::FontOptions(11.0f, juce::Font::plain));
    g.drawText("v" JucePlugin_VersionString, static_cast<int>(w - 180.0f), static_cast<int>(h - 20.0f), 150, 16, juce::Justification::centredRight);
}

juce::String EditorContent::stepPillText(int currentStep, int loopLength, bool gateOpen, bool midiMode) {
    if (midiMode) {
        return gateOpen ? "MIDI ACTIVE" : "MIDI DRY";
    }
    return "STEP " + juce::String(currentStep + 1).paddedLeft('0', 2) + "/" + juce::String(loopLength) + (gateOpen ? " ACTIVE" : " DRY");
}

bool EditorContent::keyPressed(const juce::KeyPress& key) {
    const auto code = juce::CharacterFunctions::toUpperCase(static_cast<juce::juce_wchar>(key.getKeyCode()));
    if (key.getModifiers().isCommandDown() && code == 'Z') {
        if (key.getModifiers().isShiftDown()) {
            processor_.getUndo().redo();
        } else {
            processor_.getUndo().undo();
        }
        if (suiteBar_ != nullptr) {
            suiteBar_->refresh();
        }
        return true;
    }
    return juce::Component::keyPressed(key);
}

void EditorContent::resized() {
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    const float margin = 28.0f;
    if (suiteBar_ != nullptr) {
        suiteBar_->setBounds(static_cast<int>(margin), 62, static_cast<int>(w - margin * 2.0f), 40);
    }

    // Header Controls (Right Side)
    hostSyncButton_.setBounds(static_cast<int>(w - 566.0f), 12, 116, 36);
    internalBpmSlider_.setBounds(static_cast<int>(w - 446.0f), 12, 92, 36);
    themeLabel_.setBounds(static_cast<int>(w - 350.0f), 12, 62, 36);
    themeBox_.setBounds(static_cast<int>(w - 282.0f), 12, 160, 36);
    scaleBox_.setBounds(static_cast<int>(w - 116.0f), 12, 84, 36);

    // 32-Step Beat Sequencer Section
    sequencerGrid_.setBounds(static_cast<int>(margin), 106, static_cast<int>(w - margin * 2.0f), 112);

    // Paint Brush Selector + 9 Quick Pattern Preset Buttons below Sequencer
    const float presetY = 222.0f;
    const float presetH = 36.0f;
    paintBrushLabel_.setBounds(static_cast<int>(margin), static_cast<int>(presetY), 100, static_cast<int>(presetH));
    paintBrushBox_.setBounds(static_cast<int>(margin + 104.0f), static_cast<int>(presetY), 190, static_cast<int>(presetH));

    juce::Button* presetBtns[9] = {
        &presetAllBtn_, &presetLastBeatBtn_, &presetLast4StepsBtn_, &presetLast32ndBtn_,
        &presetBackbeatBtn_, &preset16thBtn_, &presetInvertBtn_, &presetResetFxBtn_, &presetClearBtn_
    };
    const float btnsStartX = margin + 300.0f;
    const float gap = 5.5f;
    const float btnW = (w - margin - btnsStartX - gap * 8.0f) / 9.0f;
    for (int i = 0; i < 9; ++i) {
        presetBtns[i]->setBounds(static_cast<int>(btnsStartX + static_cast<float>(i) * (btnW + gap)),
                                 static_cast<int>(presetY),
                                 static_cast<int>(btnW),
                                 static_cast<int>(presetH));
    }

    const int controlY = 262;
    const int controlH = 36;
    sequenceLengthLabel_.setBounds(28, controlY, 58, controlH);
    sequenceLengthSlider_.setBounds(90, controlY, 130, controlH);
    sequenceLengthValueLabel_.setBounds(224, controlY, 72, controlH);
    sequencerSwingLabel_.setBounds(302, controlY, 50, controlH);
    sequencerSwingSlider_.setBounds(356, controlY, 130, controlH);
    sequencerSwingValueLabel_.setBounds(490, controlY, 44, controlH);
    patternRateLabel_.setBounds(540, controlY, 44, controlH);
    patternRateBox_.setBounds(588, controlY, 80, controlH);
    patternChanceLabel_.setBounds(676, controlY, 60, controlH);
    patternChanceSlider_.setBounds(740, controlY, 120, controlH);
    patternChanceValueLabel_.setBounds(864, controlY, 44, controlH);
    keepSubButton_.setBounds(916, controlY, 114, controlH);
    randomizeButton_.setBounds(1038, controlY, 136, controlH);

    // 15 Glitch FX Cards (3 Rows x 5 Columns) + Master Strip on Right
    const float detailH = 250.0f;
    const float fxTop = 306.0f;
    const float fxTotalH = h - fxTop - detailH - 22.0f - 8.0f;
    detailPanel_.setBounds(static_cast<int>(margin), static_cast<int>(h - 22.0f - detailH),
                           static_cast<int>(w - margin * 2.0f), static_cast<int>(detailH));
    const float masterW = 190.0f;
    const float fxAreaW = w - margin * 2.0f - masterW - 10.0f;

    constexpr int kCols = 5;
    constexpr int kRows = 3;
    const float colGap = 8.0f;
    const float rowGap = 8.0f;
    const float cardW = (fxAreaW - colGap * static_cast<float>(kCols - 1)) / static_cast<float>(kCols);
    const float cardH = (fxTotalH - rowGap * static_cast<float>(kRows - 1)) / static_cast<float>(kRows);

    for (size_t i = 0; i < kNumUiFx; ++i) {
        const int row = static_cast<int>(i) / kCols;
        const int col = static_cast<int>(i) % kCols;
        const float cx = margin + static_cast<float>(col) * (cardW + colGap);
        const float cy = fxTop + static_cast<float>(row) * (cardH + rowGap);

        auto& slot = fxSlots_[i];
        slot.cardBounds = { cx, cy, cardW, cardH };

        auto inner = slot.cardBounds.reduced(8.0f);
        inner.removeFromTop(34.0f);
        auto btnArea = inner.removeFromBottom(36.0f);
        slot.knob.setBounds(inner.toNearestInt());
        slot.enableBtn.setBounds(btnArea.reduced(8.0f, 0.0f).toNearestInt());
    }

    // Master Card on the far right spanning all rows
    masterCardBounds_ = { w - margin - masterW, fxTop, masterW, fxTotalH };
    auto mInner = masterCardBounds_.reduced(10.0f);
    mInner.removeFromTop(24.0f);

    masterMeterBounds_ = mInner.removeFromTop(84.0f);
    mInner.removeFromTop(4.0f);

    auto powerArea = mInner.removeFromBottom(36.0f);
    masterPowerButton_.setBounds(powerArea.toNearestInt());
    mInner.removeFromBottom(4.0f);

    // The gap keeps DE-CLICK clear of the value boxes.
    auto topKnobs = mInner.removeFromTop(148.0f);
    auto s1 = topKnobs.removeFromLeft(topKnobs.getWidth() * 0.5f);
    auto s2 = topKnobs;
    masterMixLabel_.setBounds(s1.removeFromTop(16.0f).toNearestInt());
    masterMixSlider_.setBounds(s1.reduced(2.0f, 4.0f).toNearestInt());
    masterGainLabel_.setBounds(s2.removeFromTop(16.0f).toNearestInt());
    masterGainSlider_.setBounds(s2.reduced(2.0f, 4.0f).toNearestInt());

    mInner.removeFromTop(18.0f);
    auto s3 = mInner.removeFromTop(148.0f);
    smoothLabel_.setBounds(s3.removeFromTop(16.0f).toNearestInt());
    smoothSlider_.setBounds(s3.reduced(20.0f, 4.0f).toNearestInt());
}

OpenGlitchFXEditor::OpenGlitchFXEditor(OpenGlitchFXProcessor& owner)
    : AudioProcessorEditor(&owner), processor_(owner), content_(owner) {
    // Read the saved scale first: setResizeLimits resizes the editor, and resized() stores the new scale.
    const float scale = owner.getEditorScale();
    addAndMakeVisible(content_);
    setWantsKeyboardFocus(true);
    setResizable(true, true);
    setResizeLimits(juce::roundToInt(EditorContent::kDesignWidth * EditorContent::kMinScale),
                    juce::roundToInt(EditorContent::kDesignHeight * EditorContent::kMinScale),
                    juce::roundToInt(EditorContent::kDesignWidth * EditorContent::kMaxScale),
                    juce::roundToInt(EditorContent::kDesignHeight * EditorContent::kMaxScale));
    getConstrainer()->setFixedAspectRatio(static_cast<double>(EditorContent::kDesignWidth) / EditorContent::kDesignHeight);
    setSize(juce::roundToInt(EditorContent::kDesignWidth * scale), juce::roundToInt(EditorContent::kDesignHeight * scale));
}

void OpenGlitchFXEditor::resized() {
    const float scale = std::min(static_cast<float>(getWidth()) / EditorContent::kDesignWidth,
                                 static_cast<float>(getHeight()) / EditorContent::kDesignHeight);
    content_.setTransform(juce::AffineTransform::scale(scale));
    processor_.setEditorScale(scale);
}

bool OpenGlitchFXEditor::keyPressed(const juce::KeyPress& key) {
    return content_.keyPressed(key);
}
