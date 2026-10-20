#pragma once

#include "GlitchLookAndFeel.h"
#include "../Core/ModEnvelope.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace openglitch::ui {

// Click adds a point, drag moves it, double-click or right-click deletes it.
class EnvelopeEditorComponent final : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit EnvelopeEditorComponent(const GlitchLookAndFeel& lnf);

    void setPoints(std::vector<openglitch::core::EnvelopePoint> points);
    void setAxisLabels(const juce::String& top, const juce::String& bottom, bool showCentreLine);
    void setPlayhead(float phase01, bool visible);
    void setActive(bool active);

    std::function<void(const std::vector<openglitch::core::EnvelopePoint>&)> onPointsChanged;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

private:
    juce::Rectangle<float> plotArea() const;
    juce::Point<float> toScreen(const openglitch::core::EnvelopePoint& p) const;
    openglitch::core::EnvelopePoint fromScreen(juce::Point<float> pos) const;
    int hitTestPoint(juce::Point<float> pos) const;
    void removePoint(int index);

    const GlitchLookAndFeel& lnf_;
    std::vector<openglitch::core::EnvelopePoint> points_;
    juce::String topLabel_;
    juce::String bottomLabel_;
    bool showCentreLine_ = false;
    float playhead_ = 0.0f;
    bool playheadVisible_ = false;
    bool active_ = false;
    int dragIndex_ = -1;
};

} // namespace openglitch::ui
