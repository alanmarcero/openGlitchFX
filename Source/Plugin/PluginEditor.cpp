#include "PluginEditor.h"
#include "BinaryData.h"

OpenGlitchFXEditor::OpenGlitchFXEditor(OpenGlitchFXProcessor& owner)
    : AudioProcessorEditor(&owner),
      processor_(owner),
      sequencerGrid_(owner.parameters, lookAndFeel_) {
    setLookAndFeel(&lookAndFeel_);

    // Load Gemini-generated custom Alan Marcero logo & backdrop texture from BinaryData
    customLogoImage_ = juce::ImageCache::getFromMemory(BinaryData::alan_marcero_logo_jpg,
                                                       BinaryData::alan_marcero_logo_jpgSize);
    backdropImage_ = juce::ImageCache::getFromMemory(BinaryData::plugin_backdrop_jpg,
                                                     BinaryData::plugin_backdrop_jpgSize);

    // Header Look Selector
    themeBox_.addItemList({ "Cyber Neon", "Industrial Amber", "Acid Matrix", "Arctic Glitch" }, 1);
    themeBox_.setJustificationType(juce::Justification::centred);
    themeBox_.onChange = [this]() { applyTheme(themeBox_.getSelectedItemIndex()); };
    addAndMakeVisible(themeBox_);

    themeLabel_.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    themeLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(themeLabel_);

    addAndMakeVisible(hostSyncButton_);

    internalBpmSlider_.setSliderStyle(juce::Slider::LinearBar);
    internalBpmSlider_.setTextValueSuffix(" BPM");
    addAndMakeVisible(internalBpmSlider_);

    // 32-Step Beat Sequencer Grid & Quick Pattern Buttons
    addAndMakeVisible(sequencerGrid_);

    auto setupPresetBtn = [this](juce::TextButton& btn, std::function<void()> fn) {
        btn.onClick = std::move(fn);
        addAndMakeVisible(btn);
    };

    using openglitch::core::StepPatternPreset;
    setupPresetBtn(presetAllBtn_,        [this]() { sequencerGrid_.applyPreset(StepPatternPreset::AllOn); });
    setupPresetBtn(presetLastBeatBtn_,   [this]() { sequencerGrid_.applyPreset(StepPatternPreset::LastBeat); });
    setupPresetBtn(presetLast4StepsBtn_, [this]() { sequencerGrid_.applyPreset(StepPatternPreset::LastFourSteps); });
    setupPresetBtn(presetLast32ndBtn_,   [this]() { sequencerGrid_.applyPreset(StepPatternPreset::Last32nd); });
    setupPresetBtn(presetBackbeatBtn_,   [this]() { sequencerGrid_.applyPreset(StepPatternPreset::Backbeat2And4); });
    setupPresetBtn(preset16thBtn_,       [this]() { sequencerGrid_.applyPreset(StepPatternPreset::Stutter16ths); });
    setupPresetBtn(presetInvertBtn_,     [this]() { sequencerGrid_.invertPattern(); });
    setupPresetBtn(presetClearBtn_,      [this]() { sequencerGrid_.applyPreset(StepPatternPreset::ClearAll); });

    // 10 Glitch FX Slots (2 Rows x 5 Columns)
    using namespace openglitch::params;
    struct FxMeta {
        const char* title;
        const char* sub;
        const char* amtId;
        const char* onId;
    };

    const std::array<FxMeta, kNumUiFx> metas = {{
        // Row 1: Initial 5 Glitch FX
        { "BITCRUSH",     "BITRATE & DEPTH",  kBitcrushAmt,    kBitcrushOn    },
        { "DOWNSAMPLE",   "SAMPLE & HOLD",    kDownsampleAmt,  kDownsampleOn  },
        { "STUTTER",      "BEAT REPEAT",      kStutterAmt,     kStutterOn     },
        { "TAPE STOP",    "VINYL BRAKE",      kTapeStopAmt,    kTapeStopOn    },
        { "RING MOD",     "CYBER CARRIER",    kRingModAmt,     kRingModOn     },
        // Row 2: 5 Additional Glitch FX (10 Total)
        { "REVERSE",      "BACKWARDS GRAIN",  kReverseAmt,     kReverseOn     },
        { "CHOPPER",      "RHYTHMIC SLICER",  kChopperAmt,     kChopperOn     },
        { "PITCH GLITCH", "OCTAVE ARPEGGIO",  kPitchGlitchAmt, kPitchGlitchOn },
        { "COMB RESO",    "METALLIC FLANGE",  kCombResoAmt,    kCombResoOn    },
        { "WAVEFOLD",     "HARMONIC FOLD",    kWavefoldAmt,    kWavefoldOn    }
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

        slot.knobAttach = std::make_unique<SliderAttachment>(processor_.parameters, metas[i].amtId, slot.knob);
        slot.enableAttach = std::make_unique<ButtonAttachment>(processor_.parameters, metas[i].onId, slot.enableBtn);
    }

    // Master Controls
    auto setupMasterRotary = [this](juce::Slider& s, juce::Label& l, const juce::String& suffix) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
        s.setTextValueSuffix(suffix);
        addAndMakeVisible(s);

        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        addAndMakeVisible(l);
    };

    setupMasterRotary(smoothSlider_,     smoothLabel_,     " ms");
    setupMasterRotary(masterMixSlider_,  masterMixLabel_,  " %");
    setupMasterRotary(masterGainSlider_, masterGainLabel_, " dB");
    addAndMakeVisible(masterPowerButton_);

    themeAttach_       = std::make_unique<ComboBoxAttachment>(processor_.parameters, kTheme, themeBox_);
    hostSyncAttach_    = std::make_unique<ButtonAttachment>(processor_.parameters, kHostSync, hostSyncButton_);
    internalBpmAttach_ = std::make_unique<SliderAttachment>(processor_.parameters, kInternalBpm, internalBpmSlider_);
    smoothAttach_      = std::make_unique<SliderAttachment>(processor_.parameters, kSmoothMs, smoothSlider_);
    masterMixAttach_   = std::make_unique<SliderAttachment>(processor_.parameters, kMasterMix, masterMixSlider_);
    masterGainAttach_  = std::make_unique<SliderAttachment>(processor_.parameters, kMasterGain, masterGainSlider_);
    masterPowerAttach_ = std::make_unique<ButtonAttachment>(processor_.parameters, kMasterPower, masterPowerButton_);

    processor_.parameters.addParameterListener(kTheme, this);
    applyTheme(themeBox_.getSelectedItemIndex());

    setSize(1080, 680);
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
    smoothLabel_.setColour(juce::Label::textColourId, pal.textMuted);
    masterMixLabel_.setColour(juce::Label::textColourId, pal.textMuted);
    masterGainLabel_.setColour(juce::Label::textColourId, pal.textMuted);

    sendLookAndFeelChange();
    repaint();
}

void OpenGlitchFXEditor::timerCallback() {
    const auto tel = processor_.getTelemetry();
    sequencerGrid_.setPlayheadState(tel.currentStep, tel.barPhase, tel.stepEnabled, tel.activeGate);
    repaint(0, 0, getWidth(), 62);
}

void OpenGlitchFXEditor::paint(juce::Graphics& g) {
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    const auto& pal = lookAndFeel_.getPalette();

    // 1. Base Gradient + Gemini-Generated Hardware Backdrop Texture
    juce::ColourGradient bgGrad(pal.bgTop, 0.0f, 0.0f, pal.bgBottom, 0.0f, h, false);
    g.setGradientFill(bgGrad);
    g.fillRect(0.0f, 0.0f, w, h);

    if (backdropImage_.isValid()) {
        g.setOpacity(0.28f);
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
    g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    g.drawText("32-STEP 4/4 BEAT-SYNCED 10-FX GLITCH PROCESSOR", static_cast<int>(w * 0.5f - 200.0f), 32, 400, 14, juce::Justification::centred);

    // Live Gate Status Pill Indicator in Header
    const auto tel = processor_.getTelemetry();
    const bool gateOpen = tel.activeGate > 0.25f;
    juce::Rectangle<float> statusPill(245.0f, 16.0f, 128.0f, 26.0f);
    g.setColour(gateOpen ? pal.accentPrimary.withAlpha(0.25f) : pal.stepInactiveFill);
    g.fillRoundedRectangle(statusPill, 13.0f);
    g.setColour(gateOpen ? pal.accentPrimary : pal.cardBorder);
    g.drawRoundedRectangle(statusPill, 13.0f, 1.3f);
    g.setColour(gateOpen ? pal.textBright : pal.textMuted);
    g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    const juce::String stepStr = juce::String::formatted("STEP %02d/32 %s",
                                                         tel.currentStep + 1,
                                                         gateOpen ? "ACTIVE" : "DRY");
    g.drawText(stepStr, statusPill.toNearestInt(), juce::Justification::centred);

    // Divider line
    g.setColour(pal.cardBorder);
    g.drawLine(28.0f, 58.0f, w - 28.0f, 58.0f, 1.0f);

    // 3. Draw 10 FX Slot Cards (2x5 Matrix)
    for (const auto& slot : fxSlots_) {
        const auto& r = slot.cardBounds;
        const bool active = slot.enableBtn.getToggleState() && slot.knob.getValue() > 0.1;

        g.setColour(pal.cardBg);
        g.fillRoundedRectangle(r, 6.0f);

        g.setColour(active ? pal.cardActiveBorder : pal.cardBorder);
        g.drawRoundedRectangle(r, 6.0f, active ? 1.5f : 1.0f);

        juce::Rectangle<float> hdr(r.getX() + 6.0f, r.getY() + 5.0f, r.getWidth() - 12.0f, 28.0f);
        g.setColour(pal.headerPlate);
        g.fillRoundedRectangle(hdr, 4.0f);

        g.setColour(active ? pal.accentPrimary : pal.textBright);
        g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        g.drawText(slot.title, hdr.withTrimmedBottom(12.0f).toNearestInt(), juce::Justification::centred);

        g.setColour(pal.textMuted);
        g.setFont(juce::FontOptions(8.5f, juce::Font::plain));
        g.drawText(slot.subtitle, hdr.withTrimmedTop(14.0f).toNearestInt(), juce::Justification::centred);
    }

    // 4. Master Card
    if (!masterCardBounds_.isEmpty()) {
        g.setColour(pal.cardBg);
        g.fillRoundedRectangle(masterCardBounds_, 6.0f);
        g.setColour(pal.cardBorder.brighter(0.2f));
        g.drawRoundedRectangle(masterCardBounds_, 6.0f, 1.2f);

        juce::Rectangle<float> hdr(masterCardBounds_.getX() + 6.0f, masterCardBounds_.getY() + 6.0f,
                                   masterCardBounds_.getWidth() - 12.0f, 24.0f);
        g.setColour(pal.headerPlate);
        g.fillRoundedRectangle(hdr, 4.0f);
        g.setColour(pal.accentPrimary);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText("MASTER & GATE", hdr.toNearestInt(), juce::Justification::centred);
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

    // Quick Pattern Preset Row below Sequencer
    const float presetY = 194.0f;
    const float presetH = 24.0f;
    juce::Button* presetBtns[8] = {
        &presetAllBtn_, &presetLastBeatBtn_, &presetLast4StepsBtn_, &presetLast32ndBtn_,
        &presetBackbeatBtn_, &preset16thBtn_, &presetInvertBtn_, &presetClearBtn_
    };
    const float gap = 6.0f;
    const float btnW = (w - margin * 2.0f - gap * 7.0f) / 8.0f;
    for (int i = 0; i < 8; ++i) {
        presetBtns[i]->setBounds(static_cast<int>(margin + static_cast<float>(i) * (btnW + gap)),
                                 static_cast<int>(presetY),
                                 static_cast<int>(btnW),
                                 static_cast<int>(presetH));
    }

    // 10 Glitch FX Cards (2 Rows x 5 Columns) + Master Strip on Right
    const float fxTop = 228.0f;
    const float fxTotalH = h - fxTop - 22.0f;
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
    mInner.removeFromTop(28.0f);

    auto powerArea = mInner.removeFromBottom(30.0f);
    masterPowerButton_.setBounds(powerArea.reduced(10.0f, 0.0f).toNearestInt());
    mInner.removeFromBottom(6.0f);

    const float knobSectionH = mInner.getHeight() / 3.0f;

    auto s1 = mInner.removeFromTop(knobSectionH);
    masterMixLabel_.setBounds(s1.removeFromTop(14.0f).toNearestInt());
    masterMixSlider_.setBounds(s1.toNearestInt());

    auto s2 = mInner.removeFromTop(knobSectionH);
    masterGainLabel_.setBounds(s2.removeFromTop(14.0f).toNearestInt());
    masterGainSlider_.setBounds(s2.toNearestInt());

    auto s3 = mInner;
    smoothLabel_.setBounds(s3.removeFromTop(14.0f).toNearestInt());
    smoothSlider_.setBounds(s3.toNearestInt());
}
