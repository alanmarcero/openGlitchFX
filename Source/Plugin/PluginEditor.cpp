#include "PluginEditor.h"
#include "BinaryData.h"

OpenGlitchFXEditor::OpenGlitchFXEditor(OpenGlitchFXProcessor& owner)
    : AudioProcessorEditor(&owner),
      processor_(owner),
      sequencerGrid_(owner.parameters, lookAndFeel_),
      detailPanel_(owner, lookAndFeel_) {
    setLookAndFeel(&lookAndFeel_);

    customLogoImage_ = juce::ImageCache::getFromMemory(BinaryData::alan_marcero_logo_jpg,
                                                       BinaryData::alan_marcero_logo_jpgSize);
    backdropImage_ = juce::ImageCache::getFromMemory(BinaryData::analogcyberfaceplate_png,
                                                     BinaryData::analogcyberfaceplate_pngSize);

    // Header Look Selector
    themeBox_.addItemList({ "Cyber Neon", "Industrial Amber", "Acid Matrix", "Arctic Glitch" }, 1);
    themeBox_.setJustificationType(juce::Justification::centred);
    themeBox_.onChange = [this]() { applyTheme(themeBox_.getSelectedItemIndex()); };
    addAndMakeVisible(themeBox_);

    themeLabel_.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    themeLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(themeLabel_);

    hostSyncButton_.setTooltip("Follow the host's tempo and bar position. Turn off to run on the internal BPM.");
    addAndMakeVisible(hostSyncButton_);

    internalBpmSlider_.setSliderStyle(juce::Slider::LinearBar);
    internalBpmSlider_.setTextValueSuffix(" BPM");
    internalBpmSlider_.setTooltip("Internal tempo. Used when HOST SYNC is off, or when the host reports no tempo.");
    addAndMakeVisible(internalBpmSlider_);

    // 32-Step Beat Sequencer Grid, Paint-Brush FX Selector & Quick Pattern Buttons
    addAndMakeVisible(sequencerGrid_);

    paintBrushLabel_.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    paintBrushLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(paintBrushLabel_);

    paintBrushBox_.addItem("ALL ACTIVE FX (DEFAULT)", 1);
    for (int fx = 0; fx < 10; ++fx) {
        const auto& desc = openglitch::params::kFxDescriptors[static_cast<size_t>(fx)];
        paintBrushBox_.addItem(juce::String::formatted("ONLY: %s [%s]", desc.name, desc.shortCode), fx + 2);
    }
    paintBrushBox_.setSelectedId(1, juce::dontSendNotification);
    paintBrushBox_.setTooltip("What a left-click on a pad paints. You can also click an FX card's title to pick it.");
    paintBrushBox_.onChange = [this]() {
        const int idx = paintBrushBox_.getSelectedItemIndex(); // 0 = All (-1), 1..10 = FX 0..9
        sequencerGrid_.setPaintTargetFx(idx - 1);
    };
    addAndMakeVisible(paintBrushBox_);

    auto setupPresetBtn = [this](juce::TextButton& btn, const juce::String& tooltip, std::function<void()> fn) {
        btn.onClick = std::move(fn);
        btn.setTooltip(tooltip);
        addAndMakeVisible(btn);
    };

    using openglitch::core::StepPatternPreset;
    setupPresetBtn(presetAllBtn_,        "Turn on all 32 pads. Uses the pad brush FX if one is selected.",
                   [this]() { sequencerGrid_.applyPreset(StepPatternPreset::AllOn); });
    setupPresetBtn(presetLastBeatBtn_,   "Pads 25-32: beat 4 of every bar. Uses the pad brush FX if one is selected.",
                   [this]() { sequencerGrid_.applyPreset(StepPatternPreset::LastBeat); });
    setupPresetBtn(presetLast4StepsBtn_, "Pads 29-32: the last 1/8 note of the bar. Uses the pad brush FX if one is selected.",
                   [this]() { sequencerGrid_.applyPreset(StepPatternPreset::LastFourSteps); });
    setupPresetBtn(presetLast32ndBtn_,   "Pad 32 only: the last 1/32 note of the bar. Uses the pad brush FX if one is selected.",
                   [this]() { sequencerGrid_.applyPreset(StepPatternPreset::Last32nd); });
    setupPresetBtn(presetBackbeatBtn_,   "Pads 9-16 and 25-32: beats 2 and 4. Uses the pad brush FX if one is selected.",
                   [this]() { sequencerGrid_.applyPreset(StepPatternPreset::Backbeat2And4); });
    setupPresetBtn(preset16thBtn_,       "The second 1/16 note of every 1/8 note. Uses the pad brush FX if one is selected.",
                   [this]() { sequencerGrid_.applyPreset(StepPatternPreset::Stutter16ths); });
    setupPresetBtn(presetInvertBtn_,     "Swap on and off for every pad.",
                   [this]() { sequencerGrid_.invertPattern(); });
    setupPresetBtn(presetResetFxBtn_,    "Route all 10 FX to every pad. Pad on/off stays the same.",
                   [this]() { sequencerGrid_.resetAllStepFxMasksToAll(); });
    setupPresetBtn(presetClearBtn_,      "Turn off all 32 pads.",
                   [this]() { sequencerGrid_.applyPreset(StepPatternPreset::ClearAll); });

    // 10 Glitch FX Slots (2 Rows x 5 Columns)
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
        { "TAPE STOP [TS]",    "VINYL BRAKE",      kTapeStopAmt,    kTapeStopOn,    0.0  },
        { "RING MOD [RM]",     "CYBER CARRIER",    kRingModAmt,     kRingModOn,     0.0  },
        // Row 2: 5 Additional Glitch FX (10 Total)
        { "REVERSE [RV]",      "BACKWARDS GRAIN",  kReverseAmt,     kReverseOn,     0.0  },
        { "CHOPPER [CH]",      "RHYTHMIC SLICER",  kChopperAmt,     kChopperOn,     0.0  },
        { "PITCH GLITCH [PG]", "OCTAVE ARPEGGIO",  kPitchGlitchAmt, kPitchGlitchOn, 0.0  },
        { "COMB RESO [CR]",    "METALLIC FLANGE",  kCombResoAmt,    kCombResoOn,    0.0  },
        { "WAVEFOLD [WF]",     "HARMONIC FOLD",    kWavefoldAmt,    kWavefoldOn,    0.0  }
    }};

    for (size_t i = 0; i < kNumUiFx; ++i) {
        auto& slot = fxSlots_[i];
        slot.title = metas[i].title;
        slot.subtitle = metas[i].sub;

        slot.knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slot.knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
        slot.knob.setTextValueSuffix(" %");
        addAndMakeVisible(slot.knob);

        addAndMakeVisible(slot.enableBtn);
        slot.knob.setTooltip(juce::String(metas[i].title).upToFirstOccurrenceOf(" [", false, false)
                             + " amount. At 0% this FX does nothing, even on pads routed to it.");
        slot.enableBtn.setTooltip("Turn this FX module on or off for every pad.");

        slot.knobAttach = std::make_unique<SliderAttachment>(processor_.parameters, metas[i].amtId, slot.knob);
        slot.enableAttach = std::make_unique<ButtonAttachment>(processor_.parameters, metas[i].onId, slot.enableBtn);
        slot.knob.setDoubleClickReturnValue(true, metas[i].defaultPct);
    }

    // Master Controls
    auto setupMasterRotary = [this](juce::Slider& s, juce::Label& l, const juce::String& suffix) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 15);
        s.setTextValueSuffix(suffix);
        addAndMakeVisible(s);

        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        addAndMakeVisible(l);
    };

    setupMasterRotary(smoothSlider_,     smoothLabel_,     " ms");
    setupMasterRotary(masterMixSlider_,  masterMixLabel_,  " %");
    setupMasterRotary(masterGainSlider_, masterGainLabel_, " dB");
    addAndMakeVisible(masterPowerButton_);
    smoothSlider_.setTooltip("Fade time when a pad turns on or off. Raise it if pad edges click.");
    masterMixSlider_.setTooltip("Dry/wet balance on active pads. See DRY/WET mode below.");
    masterGainSlider_.setTooltip("Output gain.");
    masterPowerButton_.setTooltip("Turn off to bypass every effect.");

    addAndMakeVisible(detailPanel_);

    themeAttach_       = std::make_unique<ComboBoxAttachment>(processor_.parameters, kTheme, themeBox_);
    hostSyncAttach_    = std::make_unique<ButtonAttachment>(processor_.parameters, kHostSync, hostSyncButton_);
    internalBpmAttach_ = std::make_unique<SliderAttachment>(processor_.parameters, kInternalBpm, internalBpmSlider_);
    smoothAttach_      = std::make_unique<SliderAttachment>(processor_.parameters, kSmoothMs, smoothSlider_);
    masterMixAttach_   = std::make_unique<SliderAttachment>(processor_.parameters, kMasterMix, masterMixSlider_);
    masterGainAttach_  = std::make_unique<SliderAttachment>(processor_.parameters, kMasterGain, masterGainSlider_);
    masterPowerAttach_ = std::make_unique<ButtonAttachment>(processor_.parameters, kMasterPower, masterPowerButton_);

    internalBpmSlider_.setDoubleClickReturnValue(true, 120.0);
    smoothSlider_.setDoubleClickReturnValue(true, 2.5);
    masterMixSlider_.setDoubleClickReturnValue(true, 100.0);
    masterGainSlider_.setDoubleClickReturnValue(true, 0.0);

    processor_.parameters.addParameterListener(kTheme, this);
    applyTheme(themeBox_.getSelectedItemIndex());

    setSize(1080, 900);
    startTimerHz(30);
}

OpenGlitchFXEditor::~OpenGlitchFXEditor() {
    processor_.parameters.removeParameterListener(openglitch::params::kTheme, this);
    stopTimer();
    setLookAndFeel(nullptr);
}

void OpenGlitchFXEditor::parameterChanged(const juce::String& parameterID, float newValue) {
    if (parameterID == openglitch::params::kTheme) {
        juce::MessageManager::callAsync([this, idx = static_cast<int>(newValue)]() {
            applyTheme(idx);
        });
    }
}

void OpenGlitchFXEditor::applyTheme(int themeIndex) {
    themeIndex = std::clamp(themeIndex, 0, 3);
    lookAndFeel_.setTheme(static_cast<openglitch::ui::Theme>(themeIndex));
    const auto& pal = lookAndFeel_.getPalette();

    themeLabel_.setColour(juce::Label::textColourId, pal.textMuted);
    paintBrushLabel_.setColour(juce::Label::textColourId, pal.accentPrimary);
    smoothLabel_.setColour(juce::Label::textColourId, pal.textMuted);
    masterMixLabel_.setColour(juce::Label::textColourId, pal.textMuted);
    masterGainLabel_.setColour(juce::Label::textColourId, pal.textMuted);

    sendLookAndFeelChange();
    repaint();
}

int OpenGlitchFXEditor::fxCardHeaderAt(juce::Point<float> position) const {
    const auto hit = std::ranges::find_if(fxSlots_, [&](const FxSlotUi& slot) {
        const auto& r = slot.cardBounds;
        return juce::Rectangle<float>(r.getX(), r.getY(), r.getWidth(), 34.0f).contains(position);
    });
    return hit == fxSlots_.end() ? -1 : static_cast<int>(std::distance(fxSlots_.begin(), hit));
}

void OpenGlitchFXEditor::mouseDown(const juce::MouseEvent& event) {
    // Clicking an FX card's top header plate toggles/selects that FX as the active Pad Brush!
    const int fx = fxCardHeaderAt(event.position);
    if (fx < 0) {
        return;
    }
    const int targetBrush = (sequencerGrid_.getPaintTargetFx() == fx) ? -1 : fx;
    paintBrushBox_.setSelectedId(targetBrush + 2, juce::sendNotificationSync);
}

juce::String OpenGlitchFXEditor::getTooltip() {
    const int fx = fxCardHeaderAt(getMouseXYRelative().toFloat());
    if (fx < 0) {
        return {};
    }
    const int pads = sequencerGrid_.countStepsRoutingToFx(fx);
    return juce::String::formatted("Click to %s this FX as the pad brush. Active on %d of 32 pads.",
                                   sequencerGrid_.getPaintTargetFx() == fx ? "clear" : "select", pads);
}

void OpenGlitchFXEditor::timerCallback() {
    const auto tel = processor_.getTelemetry();
    if (tel.clipOut) {
        clipHold_ = 1.0f;
    } else {
        clipHold_ = std::max(0.0f, clipHold_ - 0.05f);
    }
    sequencerGrid_.setPlayheadState(tel.currentStep, tel.barPhase, tel.stepEnabled, tel.activeGate);

    using namespace openglitch::params;
    sequencerGrid_.setMidiTriggerMode(processor_.parameters.getRawParameterValue(kTriggerMode)->load() > 0.5f);
    internalBpmSlider_.setAlpha(hostSyncButton_.getToggleState() ? 0.45f : 1.0f);

    if (const int version = processor_.getStateVersion(); version != lastStateVersion_) {
        lastStateVersion_ = version;
        detailPanel_.refreshFromState();
    }
    detailPanel_.setTelemetry(tel);
    repaint();
}

void OpenGlitchFXEditor::paint(juce::Graphics& g) {
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    const auto& pal = lookAndFeel_.getPalette();

    juce::ColourGradient bgGrad(pal.bgTop, 0.0f, 0.0f, pal.bgBottom, 0.0f, h, false);
    g.setGradientFill(bgGrad);
    g.fillRect(0.0f, 0.0f, w, h);

    if (backdropImage_.isValid()) {
        g.setOpacity(0.62f);
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
    g.drawText("ALAN MARCERO", 86, 12, 160, 18, juce::Justification::centredLeft);

    g.setColour(pal.textMuted);
    g.setFont(juce::FontOptions(10.0f, juce::Font::plain));
    g.drawText("CUSTOM AUDIO DSP", 86, 29, 160, 14, juce::Justification::centredLeft);

    // Center Plugin Title
    g.setColour(pal.textBright);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText("openGlitchFX", static_cast<int>(w * 0.5f - 160.0f), 8, 320, 24, juce::Justification::centred);

    g.setColour(pal.accentPrimary);
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawFittedText("32-STEP BEAT-SYNCED GLITCH PROCESSOR", static_cast<int>(w * 0.5f - 150.0f), 32, 300, 14, juce::Justification::centred, 1);

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
    g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    const juce::String stepStr = midiMode ? juce::String(gateOpen ? "MIDI ACTIVE" : "MIDI DRY")
                                          : juce::String::formatted("STEP %02d/32 %s", tel.currentStep + 1, gateOpen ? "ACTIVE" : "DRY");
    g.drawText(stepStr, statusPill.toNearestInt(), juce::Justification::centred);

    // Divider line
    g.setColour(pal.cardBorder);
    g.drawLine(28.0f, 58.0f, w - 28.0f, 58.0f, 1.0f);

    // 3. Draw 10 FX Slot Cards (2x5 Matrix) -- highlight when active on the current 1/32 pad!
    const int activeBrushFx = sequencerGrid_.getPaintTargetFx();
    for (size_t i = 0; i < kNumUiFx; ++i) {
        const auto& slot = fxSlots_[i];
        const auto& r = slot.cardBounds;
        const bool moduleArmed = slot.enableBtn.getToggleState() && slot.knob.getValue() > 0.1;
        const bool routedOnCurrentPad = ((tel.currentStepFxMask >> i) & 1u) != 0u;
        const bool firingNow = moduleArmed && routedOnCurrentPad && gateOpen;
        const bool isBrushTarget = (activeBrushFx == static_cast<int>(i));
        const int padCount = sequencerGrid_.countStepsRoutingToFx(static_cast<int>(i));
        const float fxEnv = tel.fxEnvelopes[i];

        g.setColour(pal.cardBg);
        g.fillRoundedRectangle(r, 6.0f);

        if (firingNow || isBrushTarget) {
            g.setColour((firingNow ? pal.accentSecondary : pal.accentPrimary).withAlpha(0.1f));
            g.drawRoundedRectangle(r.expanded(1.5f), 7.0f, 2.2f);
        }

        g.setColour(firingNow ? pal.accentSecondary : (isBrushTarget ? pal.stepPlayheadOutline : (moduleArmed ? pal.cardActiveBorder : pal.cardBorder)));
        g.drawRoundedRectangle(r, 6.0f, (firingNow || isBrushTarget) ? 1.8f : (moduleArmed ? 1.4f : 1.0f));

        juce::Rectangle<float> hdr(r.getX() + 6.0f, r.getY() + 5.0f, r.getWidth() - 12.0f, 28.0f);
        g.setColour(firingNow ? pal.accentSecondary.withAlpha(0.28f)
                              : (isBrushTarget ? pal.accentPrimary.withAlpha(0.22f) : pal.headerPlate));
        g.fillRoundedRectangle(hdr, 4.0f);

        // Per-FX Pad Routing Count Badge in top-right corner of header
        juce::Rectangle<float> padBadge(hdr.getRight() - 40.0f, hdr.getY() + 3.0f, 37.0f, 11.0f);
        g.setColour(padCount > 0 ? pal.accentPrimary.withAlpha(0.22f) : juce::Colours::black.withAlpha(0.35f));
        g.fillRoundedRectangle(padBadge, 2.5f);
        g.setColour(padCount > 0 ? pal.accentPrimary : pal.textMuted.withAlpha(0.6f));
        g.setFont(juce::FontOptions(7.5f, juce::Font::bold));
        g.drawText(juce::String(padCount) + (padCount == 1 ? " PAD" : " PADS"), padBadge.toNearestInt(), juce::Justification::centred);

        g.setColour(firingNow ? pal.textBright : (moduleArmed ? pal.accentPrimary : pal.textBright));
        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        g.drawFittedText(slot.title, hdr.withTrimmedBottom(12.0f).withTrimmedRight(40.0f).withTrimmedLeft(4.0f).toNearestInt(),
                         juce::Justification::centredLeft, 1);

        const bool silent = slot.enableBtn.getToggleState() && slot.knob.getValue() <= 0.1;
        const juce::String subtitle = isBrushTarget ? juce::String("PAD BRUSH ACTIVE")
                                                    : (silent ? juce::String("AMOUNT 0%: NO EFFECT") : slot.subtitle);
        g.setColour(isBrushTarget ? pal.stepPlayheadOutline : pal.textMuted);
        g.setFont(juce::FontOptions(8.2f, juce::Font::plain));
        g.drawText(subtitle, hdr.withTrimmedTop(13.0f).withTrimmedLeft(4.0f).toNearestInt(), juce::Justification::centredLeft);

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
        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        g.drawText("MASTER & TELEMETRY", hdr.toNearestInt(), juce::Justification::centred);

        if (!masterMeterBounds_.isEmpty()) {
            const auto mb = masterMeterBounds_;
            g.setColour(pal.bgBottom.withAlpha(0.9f));
            g.fillRoundedRectangle(mb, 4.0f);
            g.setColour(pal.cardBorder);
            g.drawRoundedRectangle(mb, 4.0f, 1.0f);

            auto toNormDb = [](float peak) noexcept -> float {
                if (peak <= 1e-5f) return 0.0f;
                const float db = 20.0f * std::log10(peak);
                return std::clamp((db + 36.0f) / 39.0f, 0.0f, 1.0f);
            };

            auto drawBar = [&](float y, const char* label, float val01, juce::Colour col) {
                g.setColour(pal.textMuted);
                g.setFont(juce::FontOptions(7.5f, juce::Font::bold));
                g.drawText(label, static_cast<int>(mb.getX() + 5.0f), static_cast<int>(y - 1.0f), 26, 9, juce::Justification::centredLeft);

                juce::Rectangle<float> track(mb.getX() + 32.0f, y + 1.0f, mb.getWidth() - 38.0f, 5.5f);
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
            };

            const float inPeak = std::max(tel.inputPeakL, tel.inputPeakR);
            drawBar(mb.getY() + 5.0f,  "IN",   toNormDb(inPeak),       pal.accentPrimary.withAlpha(0.85f));
            drawBar(mb.getY() + 15.0f, "OUT L", toNormDb(tel.peakL),   pal.accentPrimary);
            drawBar(mb.getY() + 25.0f, "OUT R", toNormDb(tel.peakR),   pal.accentPrimary);
            drawBar(mb.getY() + 35.0f, "GATE",  std::clamp(tel.activeGate, 0.0f, 1.0f), pal.accentSecondary);

            // Digital dB & CLIP Indicator along bottom of telemetry box
            const float maxOut = std::max(tel.peakL, tel.peakR);
            const juce::String dbStr = (maxOut <= 1e-4f) ? "OUT: -INF dB"
                                                         : juce::String::formatted("OUT: %+.1f dB", 20.0f * std::log10(maxOut));
            g.setColour(clipHold_ > 0.1f ? pal.accentSecondary : pal.textBright.withAlpha(0.85f));
            g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
            g.drawText(dbStr, static_cast<int>(mb.getX() + 6.0f), static_cast<int>(mb.getBottom() - 12.0f), 90, 10, juce::Justification::centredLeft);

            juce::Rectangle<float> clipBadge(mb.getRight() - 38.0f, mb.getBottom() - 12.0f, 32.0f, 9.5f);
            g.setColour(clipHold_ > 0.1f ? pal.accentSecondary : pal.stepInactiveFill);
            g.fillRoundedRectangle(clipBadge, 2.5f);
            g.setColour(clipHold_ > 0.1f ? pal.textBright : pal.textMuted.withAlpha(0.55f));
            g.setFont(juce::FontOptions(7.0f, juce::Font::bold));
            g.drawText("CLIP", clipBadge.toNearestInt(), juce::Justification::centred);
        }
    }
}

void OpenGlitchFXEditor::resized() {
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    const float margin = 28.0f;

    // Header Controls (Right Side)
    hostSyncButton_.setBounds(static_cast<int>(w - 395.0f), 16, 95, 26);
    internalBpmSlider_.setBounds(static_cast<int>(w - 294.0f), 16, 82, 26);
    themeLabel_.setBounds(static_cast<int>(w - 208.0f), 16, 44, 26);
    themeBox_.setBounds(static_cast<int>(w - 160.0f), 16, 128, 26);

    // 32-Step Beat Sequencer Section
    sequencerGrid_.setBounds(static_cast<int>(margin), 68, static_cast<int>(w - margin * 2.0f), 120);

    // Paint Brush Selector + 9 Quick Pattern Preset Buttons below Sequencer
    const float presetY = 194.0f;
    const float presetH = 24.0f;
    paintBrushLabel_.setBounds(static_cast<int>(margin), static_cast<int>(presetY), 72, static_cast<int>(presetH));
    paintBrushBox_.setBounds(static_cast<int>(margin + 75.0f), static_cast<int>(presetY), 188, static_cast<int>(presetH));

    juce::Button* presetBtns[9] = {
        &presetAllBtn_, &presetLastBeatBtn_, &presetLast4StepsBtn_, &presetLast32ndBtn_,
        &presetBackbeatBtn_, &preset16thBtn_, &presetInvertBtn_, &presetResetFxBtn_, &presetClearBtn_
    };
    const float btnsStartX = margin + 270.0f;
    const float gap = 4.5f;
    const float btnW = (w - margin - btnsStartX - gap * 8.0f) / 9.0f;
    for (int i = 0; i < 9; ++i) {
        presetBtns[i]->setBounds(static_cast<int>(btnsStartX + static_cast<float>(i) * (btnW + gap)),
                                 static_cast<int>(presetY),
                                 static_cast<int>(btnW),
                                 static_cast<int>(presetH));
    }

    // 10 Glitch FX Cards (2 Rows x 5 Columns) + Master Strip on Right
    const float detailH = 196.0f;
    const float fxTop = 228.0f;
    const float fxTotalH = h - fxTop - detailH - 22.0f - 8.0f;
    detailPanel_.setBounds(static_cast<int>(margin), static_cast<int>(h - 22.0f - detailH),
                           static_cast<int>(w - margin * 2.0f), static_cast<int>(detailH));
    const float masterW = 190.0f;
    const float fxAreaW = w - margin * 2.0f - masterW - 10.0f;

    constexpr int kCols = 5;
    constexpr int kRows = 2;
    const float colGap = 8.0f;
    const float rowGap = 8.0f;
    const float cardW = (fxAreaW - colGap * static_cast<float>(kCols - 1)) / static_cast<float>(kCols);
    const float cardH = (fxTotalH - rowGap) / static_cast<float>(kRows);

    for (size_t i = 0; i < kNumUiFx; ++i) {
        const int row = static_cast<int>(i) / kCols;
        const int col = static_cast<int>(i) % kCols;
        const float cx = margin + static_cast<float>(col) * (cardW + colGap);
        const float cy = fxTop + static_cast<float>(row) * (cardH + rowGap);

        auto& slot = fxSlots_[i];
        slot.cardBounds = { cx, cy, cardW, cardH };

        auto inner = slot.cardBounds.reduced(8.0f);
        inner.removeFromTop(30.0f);
        auto btnArea = inner.removeFromBottom(24.0f);
        slot.knob.setBounds(inner.toNearestInt());
        slot.enableBtn.setBounds(btnArea.reduced(14.0f, 0.0f).toNearestInt());
    }

    // Master Card on the far right spanning both rows
    masterCardBounds_ = { w - margin - masterW, fxTop, masterW, fxTotalH };
    auto mInner = masterCardBounds_.reduced(10.0f);
    mInner.removeFromTop(24.0f);

    masterMeterBounds_ = mInner.removeFromTop(56.0f);
    mInner.removeFromTop(4.0f);

    auto powerArea = mInner.removeFromBottom(28.0f);
    masterPowerButton_.setBounds(powerArea.reduced(10.0f, 0.0f).toNearestInt());
    mInner.removeFromBottom(4.0f);

    // The gap keeps DE-CLICK clear of the value boxes.
    auto topKnobs = mInner.removeFromTop(mInner.getHeight() * 0.5f);
    auto s1 = topKnobs.removeFromLeft(topKnobs.getWidth() * 0.5f);
    auto s2 = topKnobs;
    masterMixLabel_.setBounds(s1.removeFromTop(13.0f).toNearestInt());
    masterMixSlider_.setBounds(s1.toNearestInt());
    masterGainLabel_.setBounds(s2.removeFromTop(13.0f).toNearestInt());
    masterGainSlider_.setBounds(s2.toNearestInt());

    auto s3 = mInner.withTrimmedTop(6.0f);
    smoothLabel_.setBounds(s3.removeFromTop(13.0f).toNearestInt());
    smoothSlider_.setBounds(s3.toNearestInt());
}
