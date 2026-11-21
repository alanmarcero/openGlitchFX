#pragma once

#include "GlitchLookAndFeel.h"
#include "../Core/ModEnvelope.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace openglitch::ui {

// Click adds a point, drag moves it, double-click or right-click deletes it.
// Points snap to 1/16 of the span and to the value grid; Shift moves them freely.
class EnvelopeEditorComponent final : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit EnvelopeEditorComponent(const GlitchLookAndFeel& lnf);

    void setPoints(std::vector<openglitch::core::EnvelopePoint> points);
    void setAxis(const juce::String& top, const juce::String& bottom, bool showCentreLine,
                 int valueSteps, juce::String (*describeValue)(float));
    void setPlayhead(float phase01, bool visible);
    void setActive(bool active);
    // The dragged point's value and time, such as "+7 st  5/16". Empty when nothing is dragged.
    juce::String dragReadout() const;

    std::function<void(const std::vector<openglitch::core::EnvelopePoint>&)> onPointsChanged;
    std::function<void()> onEditBegan;
    std::function<void()> onEditEnded;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

private:
    juce::Rectangle<float> plotArea() const;
    juce::Point<float> toScreen(const openglitch::core::EnvelopePoint& p) const;
    openglitch::core::EnvelopePoint fromScreen(juce::Point<float> pos, bool snap) const;
    int hitTestPoint(juce::Point<float> pos) const;
    void beginEdit();
    void endEdit();
    void removePoint(int index);

    const GlitchLookAndFeel& lnf_;
    std::vector<openglitch::core::EnvelopePoint> points_;
    juce::String topLabel_;
    juce::String bottomLabel_;
    bool showCentreLine_ = false;
    int valueSteps_ = 0;
    juce::String (*describeValue_)(float) = nullptr;
    float playhead_ = 0.0f;
    bool playheadVisible_ = false;
    bool active_ = false;
    bool editing_ = false;
    int dragIndex_ = -1;
};

} // namespace openglitch::ui
