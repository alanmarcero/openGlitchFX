#include "DetailPanelComponent.h"

namespace openglitch::ui {

using openglitch::core::EnvTarget;
using openglitch::core::ModEnvelope;

namespace {

constexpr float kCardHeaderH = 30.0f;
constexpr float kCaptionH = 12.0f;
constexpr float kControlH = 22.0f;
constexpr float kRowGap = 6.0f;
constexpr float kColGap = 6.0f;

struct EnvAxis {
    const char* top;
    const char* bottom;
    bool centreLine;
};

// Order matches EnvTarget
constexpr std::array<EnvAxis, 4> kEnvAxes = {{
    { "100%",   "0%",     false },
    { "R",      "L",      true  },
    { "CUTOFF", "-8 OCT", false },
    { "+12 ST", "-12 ST", true  }
}};

// Each control skips the caption row, so the rows align.
void place(juce::Component& component, juce::Rectangle<float> area) {
    component.setBounds(area.withTrimmedTop(kCaptionH).toNearestInt());
}

} // namespace

DetailPanelComponent::DetailPanelComponent(OpenGlitchFXProcessor& processor, const GlitchLookAndFeel& lnf)
    : processor_(processor), lnf_(lnf), envEditor_(lnf) {
    using namespace openglitch::params;

    cards_[0] = { "BUFFER", "STUTTER OPTIONS", {} };
    cards_[1] = { "TAPE", "TAPE STOP OPTIONS", {} };
    cards_[2] = { "ENVELOPES", "DRAWN MODULATION OF THE WET SIGNAL", {} };
    cards_[3] = { "FILTER & TRIGGER", "", {} };

    setupCombo(stutterSyncBox_, kStutterSync, "SIZE",
               "Stutter slice length. KNOB lets the STUTTER amount set it; a note value locks it to the tempo.");
    setupCombo(stutterReverseBox_, kStutterReverse, "REVERSE",
               "Play repeats backwards: every repeat (ALL) or every second one (ALTERNATE).");
    setupBar(stutterGateSlider_, kStutterGate, "GATE",
             "How much of each repeat is heard. Lower values cut each repeat short.");
    setupBar(stutterPitchSlider_, kStutterPitch, "PITCH / REPEAT",
             "Pitch change added on every repeat, in semitones. Negative values pitch each repeat further down.");
    setupToggle(stutterAdvanceButton_, kStutterAdvance,
                "Each repeat starts half a slice later, so the stutter walks forward through the audio.");

    setupCombo(tapeDirectionBox_, kTapeDirection, "DIRECTION",
               "STOP slows the tape to a halt. START spins it up from a standstill.");
    setupCombo(tapeCurveBox_, kTapeCurve, "CURVE",
               "SMOOTH ramps the speed continuously. STEPPED moves it in 8 audible steps.");
    setupCombo(tapeLengthBox_, kTapeLength, "LENGTH",
               "Stop length. PAD SPAN stretches it over the active pads; a note value fixes it to the tempo.");
    setupToggle(tapeSlipButton_, kTapeSlip, "Adds wow and flutter, like a worn tape or a warped record.");
    setupToggle(tapeLowpassButton_, kTapeLowpass, "Low-pass filter that rolls off highs as the tape slows down.");
    setupToggle(tapeFadeButton_, kTapeFade, "Lowers the volume as the tape slows down.");

    setupCombo(filterModeBox_, kFilterMode, "FILTER",
               "Filter on the glitched signal while a pad or MIDI note is active. The FILTER envelope moves its cutoff.");
    setupBar(filterCutoffSlider_, kFilterCutoff, "CUTOFF", "Filter cutoff frequency.");
    setupBar(filterResoSlider_, kFilterReso, "RESONANCE", "Filter resonance.");
    setupCombo(triggerModeBox_, kTriggerMode, "TRIGGER",
               "SEQUENCER uses the 32 pads. MIDI ignores the pads: any note triggers every FX, notes 60-69 trigger one FX each.");
    setupCombo(triggerStartBox_, kTriggerStart, "MIDI START",
               "FREE starts on the note. SYNCED waits for the next 1/32 step.");
    setupCombo(mixModeBox_, kMixMode, "DRY/WET",
               "INSERT replaces the dry signal on active pads. PARALLEL keeps the dry signal and adds the glitch on top.");

    for (size_t i = 0; i < envTabs_.size(); ++i) {
        auto& tab = envTabs_[i];
        tab.setButtonText(kEnvDescriptors[i].name);
        tab.setClickingTogglesState(true);
        tab.setRadioGroupId(4201);
        tab.onClick = [this, i]() { selectEnvelope(static_cast<int>(i)); };
        addAndMakeVisible(tab);
    }
    envTabs_[0].setTooltip("Volume of the glitched signal across the pad span or note length.");
    envTabs_[1].setTooltip("Left/right balance of the glitched signal. The centre line is no change.");
    envTabs_[2].setTooltip("Filter cutoff. The top is the CUTOFF setting, the bottom is 8 octaves lower. Needs FILTER on.");
    envTabs_[3].setTooltip("Stutter playback pitch, up to one octave either way. The centre line is no change. Needs STUTTER.");

    envEditor_.onPointsChanged = [this](const std::vector<openglitch::core::EnvelopePoint>& points) {
        processor_.setEnvelopePoints(static_cast<EnvTarget>(selectedEnv_), points);
    };
    addAndMakeVisible(envEditor_);

    envOnButton_.setTooltip("Turn this envelope on.");
    addAndMakeVisible(envOnButton_);
    envRateBox_.addItemList(kNoteLengthLabels, 1);
    envRateBox_.setTooltip("PAD SPAN runs the envelope once across each block of active pads. A note value loops it in time with the song.");
    captions_.emplace_back(&envRateBox_, "LENGTH");
    addAndMakeVisible(envRateBox_);
    envDepthSlider_.setSliderStyle(juce::Slider::LinearBar);
    envDepthSlider_.setTextValueSuffix(" %");
    envDepthSlider_.setTooltip("How far the envelope moves its target.");
    captions_.emplace_back(&envDepthSlider_, "DEPTH");
    addAndMakeVisible(envDepthSlider_);
    envResetButton_.setTooltip("Restore this envelope's default shape.");
    envResetButton_.onClick = [this]() {
        const auto target = static_cast<EnvTarget>(selectedEnv_);
        processor_.setEnvelopePoints(target, ModEnvelope::defaultPoints(target));
        envEditor_.setPoints(processor_.getEnvelopePoints(target));
    };
    addAndMakeVisible(envResetButton_);

    envTabs_[0].setToggleState(true, juce::dontSendNotification);
    selectEnvelope(0);
}

void DetailPanelComponent::setupCombo(juce::ComboBox& box, const char* paramId, const juce::String& caption, const juce::String& tooltip) {
    auto* param = dynamic_cast<juce::AudioParameterChoice*>(processor_.parameters.getParameter(paramId));
    jassert(param != nullptr);
    box.addItemList(param->choices, 1);
    box.setTooltip(tooltip);
    captions_.emplace_back(&box, caption);
    addAndMakeVisible(box);
    comboAttachments_.push_back(std::make_unique<ComboBoxAttachment>(processor_.parameters, paramId, box));
}

void DetailPanelComponent::setupToggle(juce::ToggleButton& button, const char* paramId, const juce::String& tooltip) {
    button.setTooltip(tooltip);
    addAndMakeVisible(button);
    buttonAttachments_.push_back(std::make_unique<ButtonAttachment>(processor_.parameters, paramId, button));
}

void DetailPanelComponent::setupBar(juce::Slider& slider, const char* paramId, const juce::String& caption, const juce::String& tooltip) {
    slider.setSliderStyle(juce::Slider::LinearBar);
    slider.setTooltip(tooltip);
    captions_.emplace_back(&slider, caption);
    addAndMakeVisible(slider);
    sliderAttachments_.push_back(std::make_unique<SliderAttachment>(processor_.parameters, paramId, slider));
    if (auto* param = processor_.parameters.getParameter(paramId)) {
        slider.setDoubleClickReturnValue(true, param->convertFrom0to1(param->getDefaultValue()));
        slider.setTextValueSuffix(" " + param->getLabel());
    }
}

void DetailPanelComponent::selectEnvelope(int index) {
    selectedEnv_ = std::clamp(index, 0, static_cast<int>(envTabs_.size()) - 1);
    const auto& desc = openglitch::params::kEnvDescriptors[static_cast<size_t>(selectedEnv_)];
    const auto& axis = kEnvAxes[static_cast<size_t>(selectedEnv_)];

    envOnAttach_ = std::make_unique<ButtonAttachment>(processor_.parameters, desc.onParamId, envOnButton_);
    envRateAttach_ = std::make_unique<ComboBoxAttachment>(processor_.parameters, desc.rateParamId, envRateBox_);
    envDepthAttach_ = std::make_unique<SliderAttachment>(processor_.parameters, desc.depthParamId, envDepthSlider_);
    envDepthSlider_.setDoubleClickReturnValue(true, 100.0);

    envEditor_.setAxisLabels(axis.top, axis.bottom, axis.centreLine);
    envEditor_.setPoints(processor_.getEnvelopePoints(static_cast<EnvTarget>(selectedEnv_)));
}

void DetailPanelComponent::refreshFromState() {
    envEditor_.setPoints(processor_.getEnvelopePoints(static_cast<EnvTarget>(selectedEnv_)));
}

void DetailPanelComponent::setTelemetry(const openglitch::core::MeterTelemetry& telemetry) {
    const auto& pal = lnf_.getPalette();
    for (size_t i = 0; i < envTabs_.size(); ++i) {
        const bool on = processor_.parameters.getRawParameterValue(openglitch::params::kEnvDescriptors[i].onParamId)->load() > 0.5f;
        envTabs_[i].setColour(juce::TextButton::textColourOffId, on ? pal.accentPrimary : pal.textMuted);
        envTabs_[i].setColour(juce::TextButton::textColourOnId, on ? pal.accentPrimary : pal.textBright);
    }
    envEditor_.setActive(envOnButton_.getToggleState());
    envEditor_.setPlayhead(telemetry.modEnvelopePhases[static_cast<size_t>(selectedEnv_)], telemetry.activeGate > 0.25f);
}

void DetailPanelComponent::resized() {
    const auto bounds = getLocalBounds().toFloat();
    constexpr float kCardGap = 8.0f;
    const std::array<float, 4> widths = { 230.0f, 230.0f, 0.0f, 230.0f };
    const float envWidth = bounds.getWidth() - widths[0] - widths[1] - widths[3] - 3.0f * kCardGap;

    float x = bounds.getX();
    for (size_t i = 0; i < cards_.size(); ++i) {
        const float w = (i == 2) ? envWidth : widths[i];
        cards_[i].bounds = { x, bounds.getY(), w, bounds.getHeight() };
        x += w + kCardGap;
    }

    auto inner = [](const Card& card) {
        return card.bounds.reduced(8.0f).withTrimmedTop(kCardHeaderH - 2.0f);
    };
    const float rowH = kCaptionH + kControlH;
    auto row = [&](juce::Rectangle<float> area, int rowIndex) {
        return area.withY(area.getY() + static_cast<float>(rowIndex) * (rowH + kRowGap)).withHeight(rowH);
    };
    auto columns = [](juce::Rectangle<float> area, int count, int index) {
        const float w = (area.getWidth() - kColGap * static_cast<float>(count - 1)) / static_cast<float>(count);
        return area.withX(area.getX() + static_cast<float>(index) * (w + kColGap)).withWidth(w);
    };

    const auto buffer = inner(cards_[0]);
    place(stutterSyncBox_,      columns(row(buffer, 0), 2, 0));
    place(stutterReverseBox_,   columns(row(buffer, 0), 2, 1));
    place(stutterGateSlider_,   columns(row(buffer, 1), 2, 0));
    place(stutterPitchSlider_,  columns(row(buffer, 1), 2, 1));
    place(stutterAdvanceButton_, columns(row(buffer, 2), 2, 0));

    const auto tape = inner(cards_[1]);
    place(tapeDirectionBox_,  columns(row(tape, 0), 2, 0));
    place(tapeCurveBox_,      columns(row(tape, 0), 2, 1));
    place(tapeLengthBox_,     columns(row(tape, 1), 2, 0));
    place(tapeSlipButton_,    columns(row(tape, 2), 3, 0));
    place(tapeLowpassButton_, columns(row(tape, 2), 3, 1));
    place(tapeFadeButton_,    columns(row(tape, 2), 3, 2));

    auto env = inner(cards_[2]);
    auto tabsRow = env.removeFromTop(kControlH);
    for (size_t i = 0; i < envTabs_.size(); ++i) {
        envTabs_[i].setBounds(columns(tabsRow, static_cast<int>(envTabs_.size()), static_cast<int>(i)).toNearestInt());
    }
    env.removeFromTop(kRowGap);
    const auto controlsRow = env.removeFromBottom(rowH);
    env.removeFromBottom(kRowGap);
    envEditor_.setBounds(env.toNearestInt());
    auto envControls = controlsRow;
    place(envOnButton_, envControls.removeFromLeft(50.0f));
    place(envResetButton_, envControls.removeFromRight(54.0f));
    envControls.reduce(kColGap, 0.0f);
    place(envRateBox_, envControls.removeFromLeft(envControls.getWidth() * 0.58f));
    envControls.removeFromLeft(kColGap);
    place(envDepthSlider_, envControls);

    const auto filter = inner(cards_[3]);
    place(filterModeBox_,      columns(row(filter, 0), 3, 0));
    place(filterCutoffSlider_, columns(row(filter, 0), 3, 1));
    place(filterResoSlider_,   columns(row(filter, 0), 3, 2));
    place(triggerModeBox_,     columns(row(filter, 1), 2, 0));
    place(triggerStartBox_,    columns(row(filter, 1), 2, 1));
    place(mixModeBox_,         columns(row(filter, 2), 2, 0));
}

void DetailPanelComponent::paint(juce::Graphics& g) {
    const auto& pal = lnf_.getPalette();

    for (const auto& card : cards_) {
        g.setColour(pal.cardBg);
        g.fillRoundedRectangle(card.bounds, 6.0f);
        g.setColour(pal.cardBorder);
        g.drawRoundedRectangle(card.bounds, 6.0f, 1.0f);

        const juce::Rectangle<float> hdr(card.bounds.getX() + 6.0f, card.bounds.getY() + 5.0f, card.bounds.getWidth() - 12.0f, 20.0f);
        g.setColour(pal.headerPlate);
        g.fillRoundedRectangle(hdr, 4.0f);
        g.setColour(pal.accentPrimary);
        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        g.drawText(card.title, hdr.reduced(8.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);
        g.setColour(pal.textMuted);
        g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
        g.drawText(card.subtitle, hdr.reduced(8.0f, 0.0f).toNearestInt(), juce::Justification::centredRight);
    }

    g.setColour(pal.textMuted);
    g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    for (const auto& [component, caption] : captions_) {
        const auto b = component->getBounds();
        g.drawText(caption, b.getX(), b.getY() - static_cast<int>(kCaptionH), b.getWidth(), static_cast<int>(kCaptionH),
                   juce::Justification::centredLeft);
    }
}

} // namespace openglitch::ui
