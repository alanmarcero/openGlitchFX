#include "EnvelopeEditorComponent.h"
#include <algorithm>

namespace openglitch::ui {

using openglitch::core::EnvelopePoint;
using openglitch::core::ModEnvelope;

namespace {
constexpr float kPointRadius = 4.0f;
constexpr float kHitRadius = 8.0f;
constexpr int kMinPoints = 2;
} // namespace

EnvelopeEditorComponent::EnvelopeEditorComponent(const GlitchLookAndFeel& lnf) : lnf_(lnf) {
    setTooltip("Click to add a point, drag to move it, double-click or right-click a point to delete it.");
}

void EnvelopeEditorComponent::setPoints(std::vector<EnvelopePoint> points) {
    std::ranges::sort(points, {}, &EnvelopePoint::x);
    points_ = std::move(points);
    repaint();
}

void EnvelopeEditorComponent::setAxisLabels(const juce::String& top, const juce::String& bottom, bool showCentreLine) {
    topLabel_ = top;
    bottomLabel_ = bottom;
    showCentreLine_ = showCentreLine;
    repaint();
}

void EnvelopeEditorComponent::setPlayhead(float phase01, bool visible) {
    if (visible == playheadVisible_ && std::abs(phase01 - playhead_) < 0.004f) {
        return;
    }
    playhead_ = std::clamp(phase01, 0.0f, 1.0f);
    playheadVisible_ = visible;
    repaint();
}

void EnvelopeEditorComponent::setActive(bool active) {
    active_ = active;
    repaint();
}

juce::Rectangle<float> EnvelopeEditorComponent::plotArea() const {
    return getLocalBounds().toFloat().reduced(kPointRadius + 2.0f);
}

juce::Point<float> EnvelopeEditorComponent::toScreen(const EnvelopePoint& p) const {
    const auto area = plotArea();
    return { area.getX() + p.x * area.getWidth(), area.getBottom() - p.y * area.getHeight() };
}

EnvelopePoint EnvelopeEditorComponent::fromScreen(juce::Point<float> pos) const {
    const auto area = plotArea();
    return { std::clamp((pos.x - area.getX()) / area.getWidth(), 0.0f, 1.0f),
             std::clamp((area.getBottom() - pos.y) / area.getHeight(), 0.0f, 1.0f) };
}

int EnvelopeEditorComponent::hitTestPoint(juce::Point<float> pos) const {
    const auto hit = std::ranges::find_if(points_, [&](const EnvelopePoint& p) {
        return toScreen(p).getDistanceFrom(pos) <= kHitRadius;
    });
    return hit == points_.end() ? -1 : static_cast<int>(std::distance(points_.begin(), hit));
}

void EnvelopeEditorComponent::removePoint(int index) {
    if (index < 0 || static_cast<int>(points_.size()) <= kMinPoints) {
        return;
    }
    points_.erase(points_.begin() + index);
    if (onPointsChanged) {
        onPointsChanged(points_);
    }
    repaint();
}

void EnvelopeEditorComponent::mouseDown(const juce::MouseEvent& event) {
    const int hit = hitTestPoint(event.position);
    if (event.mods.isPopupMenu()) {
        removePoint(hit);
        return;
    }
    if (hit >= 0) {
        dragIndex_ = hit;
        return;
    }
    if (static_cast<int>(points_.size()) >= ModEnvelope::kMaxPoints) {
        return;
    }
    const auto added = fromScreen(event.position);
    const auto insertAt = std::ranges::find_if(points_, [&](const EnvelopePoint& p) { return p.x > added.x; });
    dragIndex_ = static_cast<int>(std::distance(points_.begin(), points_.insert(insertAt, added)));
    if (onPointsChanged) {
        onPointsChanged(points_);
    }
    repaint();
}

void EnvelopeEditorComponent::mouseDrag(const juce::MouseEvent& event) {
    if (dragIndex_ < 0 || event.mods.isPopupMenu()) {
        return;
    }
    // Points keep their time order.
    const auto index = static_cast<size_t>(dragIndex_);
    const float minX = index == 0 ? 0.0f : points_[index - 1].x;
    const float maxX = index + 1 == points_.size() ? 1.0f : points_[index + 1].x;
    auto moved = fromScreen(event.position);
    moved.x = std::clamp(moved.x, minX, maxX);
    points_[index] = moved;
    if (onPointsChanged) {
        onPointsChanged(points_);
    }
    repaint();
}

void EnvelopeEditorComponent::mouseUp(const juce::MouseEvent& /*event*/) {
    dragIndex_ = -1;
}

void EnvelopeEditorComponent::mouseDoubleClick(const juce::MouseEvent& event) {
    removePoint(hitTestPoint(event.position));
}

void EnvelopeEditorComponent::paint(juce::Graphics& g) {
    const auto& pal = lnf_.getPalette();
    const auto bounds = getLocalBounds().toFloat();
    const auto area = plotArea();

    g.setColour(pal.bgBottom.withAlpha(0.9f));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(pal.cardBorder);
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

    // The centre line is "no change" for pan and pitch.
    g.setColour(pal.cardBorder.withAlpha(0.5f));
    for (int i = 1; i < 4; ++i) {
        const float x = area.getX() + area.getWidth() * static_cast<float>(i) / 4.0f;
        g.drawVerticalLine(static_cast<int>(x), area.getY(), area.getBottom());
    }
    if (showCentreLine_) {
        g.setColour(pal.textMuted.withAlpha(0.45f));
        g.drawHorizontalLine(static_cast<int>(area.getCentreY()), area.getX(), area.getRight());
    }

    g.setColour(pal.textMuted);
    g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
    const auto labelArea = area.reduced(kHitRadius + 2.0f, 0.0f);
    g.drawText(topLabel_, labelArea.withHeight(10.0f).toNearestInt(), juce::Justification::topLeft);
    g.drawText(bottomLabel_, labelArea.withTrimmedTop(labelArea.getHeight() - 10.0f).toNearestInt(), juce::Justification::bottomLeft);

    if (points_.empty()) {
        return;
    }

    juce::Path curve;
    curve.startNewSubPath(area.getX(), toScreen(points_.front()).y);
    for (const auto& p : points_) {
        curve.lineTo(toScreen(p));
    }
    curve.lineTo(area.getRight(), toScreen(points_.back()).y);

    const auto lineColour = active_ ? pal.accentPrimary : pal.textMuted;
    juce::Path fill(curve);
    fill.lineTo(area.getRight(), area.getBottom());
    fill.lineTo(area.getX(), area.getBottom());
    fill.closeSubPath();
    g.setColour(lineColour.withAlpha(0.14f));
    g.fillPath(fill);
    g.setColour(lineColour);
    g.strokePath(curve, juce::PathStrokeType(1.6f));

    for (const auto& p : points_) {
        const auto c = toScreen(p);
        g.setColour(pal.textBright);
        g.fillEllipse(c.x - kPointRadius, c.y - kPointRadius, kPointRadius * 2.0f, kPointRadius * 2.0f);
        g.setColour(lineColour);
        g.drawEllipse(c.x - kPointRadius, c.y - kPointRadius, kPointRadius * 2.0f, kPointRadius * 2.0f, 1.2f);
    }

    if (playheadVisible_) {
        const float x = area.getX() + playhead_ * area.getWidth();
        g.setColour(pal.accentSecondary);
        g.drawLine(x, area.getY(), x, area.getBottom(), 1.5f);
    }
}

} // namespace openglitch::ui
