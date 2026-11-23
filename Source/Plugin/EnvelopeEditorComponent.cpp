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

void EnvelopeEditorComponent::setAxis(const juce::String& top, const juce::String& bottom, bool showCentreLine,
                                      int valueSteps, juce::String (*describeValue)(float)) {
    topLabel_ = top;
    bottomLabel_ = bottom;
    showCentreLine_ = showCentreLine;
    valueSteps_ = valueSteps;
    describeValue_ = describeValue;
    repaint();
}

juce::String EnvelopeEditorComponent::dragReadout() const {
    if (dragIndex_ < 0 || describeValue_ == nullptr) {
        return {};
    }
    const auto& point = points_[static_cast<size_t>(dragIndex_)];
    const float sixteenths = point.x * 16.0f;
    const bool onGrid = std::abs(sixteenths - std::round(sixteenths)) < 0.01f;
    return describeValue_(point.y) + "  " + juce::String(sixteenths, onGrid ? 0 : 1) + "/16";
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

EnvelopePoint EnvelopeEditorComponent::fromScreen(juce::Point<float> pos, bool snap) const {
    const auto area = plotArea();
    const float x = std::clamp((pos.x - area.getX()) / area.getWidth(), 0.0f, 1.0f);
    const float y = std::clamp((area.getBottom() - pos.y) / area.getHeight(), 0.0f, 1.0f);
    if (!snap) {
        return { x, y };
    }
    const float steps = static_cast<float>(valueSteps_);
    return { std::round(x * 16.0f) / 16.0f, valueSteps_ > 0 ? std::round(y * steps) / steps : y };
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

void EnvelopeEditorComponent::beginEdit() {
    if (!editing_) {
        editing_ = true;
        if (onEditBegan) {
            onEditBegan();
        }
    }
}

void EnvelopeEditorComponent::endEdit() {
    if (editing_) {
        editing_ = false;
        if (onEditEnded) {
            onEditEnded();
        }
    }
}

void EnvelopeEditorComponent::mouseMove(const juce::MouseEvent& event) {
    const int hit = hitTestPoint(event.position);
    setMouseCursor(hit >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::CrosshairCursor);
    if (hit != hoveredIndex_) {
        hoveredIndex_ = hit;
        repaint();
    }
}

void EnvelopeEditorComponent::mouseExit(const juce::MouseEvent& /*event*/) {
    setMouseCursor(juce::MouseCursor::NormalCursor);
    if (hoveredIndex_ != -1) {
        hoveredIndex_ = -1;
        repaint();
    }
}

void EnvelopeEditorComponent::mouseDown(const juce::MouseEvent& event) {
    beginEdit();
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
    const auto added = fromScreen(event.position, !event.mods.isShiftDown());
    const auto insertAt = std::ranges::find_if(points_, [&](const EnvelopePoint& p) { return p.x > added.x; });
    // Insert first: the insert can reallocate, so a begin() taken before it is stale.
    const auto inserted = points_.insert(insertAt, added);
    dragIndex_ = static_cast<int>(std::distance(points_.begin(), inserted));
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
    auto moved = fromScreen(event.position, !event.mods.isShiftDown());
    moved.x = std::clamp(moved.x, minX, maxX);
    points_[index] = moved;
    if (onPointsChanged) {
        onPointsChanged(points_);
    }
    repaint();
}

void EnvelopeEditorComponent::mouseUp(const juce::MouseEvent& /*event*/) {
    dragIndex_ = -1;
    endEdit();
    repaint();
}

// JUCE sends the double-click after the second mouseUp has ended the gesture.
void EnvelopeEditorComponent::mouseDoubleClick(const juce::MouseEvent& event) {
    beginEdit();
    removePoint(hitTestPoint(event.position));
    endEdit();
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

    if (!points_.empty()) {
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

        for (size_t i = 0; i < points_.size(); ++i) {
            const auto c = toScreen(points_[i]);
            const bool highlighted = (static_cast<int>(i) == hoveredIndex_ || static_cast<int>(i) == dragIndex_);
            const float radius = highlighted ? kPointRadius + 1.5f : kPointRadius;
            if (highlighted) {
                g.setColour(lineColour.withAlpha(0.28f));
                g.fillEllipse(c.x - radius - 2.5f, c.y - radius - 2.5f, (radius + 2.5f) * 2.0f, (radius + 2.5f) * 2.0f);
            }
            g.setColour(pal.textBright);
            g.fillEllipse(c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
            g.setColour(lineColour);
            g.drawEllipse(c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f, highlighted ? 1.8f : 1.2f);
        }
    }

    const juce::Font labelFont(juce::FontOptions(11.5f, juce::Font::bold));
    g.setFont(labelFont);
    auto drawAxisLabel = [&](const juce::String& text, bool isTop) {
        if (text.isEmpty()) {
            return;
        }
        const float textW = std::ceil(juce::GlyphArrangement::getStringWidth(labelFont, text)) + 8.0f;
        const float x = area.getX() + kHitRadius + 6.0f;
        const float y = isTop ? area.getY() + 1.0f : area.getBottom() - 16.0f;
        const juce::Rectangle<float> pill(x, y, textW, 15.0f);
        g.setColour(pal.bgBottom.withAlpha(0.84f));
        g.fillRoundedRectangle(pill, 3.0f);
        g.setColour(pal.textMuted);
        g.drawText(text, pill.toNearestInt(), juce::Justification::centred);
    };
    drawAxisLabel(topLabel_, true);
    drawAxisLabel(bottomLabel_, false);

    if (points_.empty()) {
        return;
    }

    if (playheadVisible_) {
        const float x = area.getX() + playhead_ * area.getWidth();
        g.setColour(pal.accentSecondary);
        g.drawLine(x, area.getY(), x, area.getBottom(), 1.5f);
    }

    if (const auto readout = dragReadout(); readout.isNotEmpty()) {
        const auto point = toScreen(points_[static_cast<size_t>(dragIndex_)]);
        const auto box = juce::Rectangle<float>(112.0f, 18.0f).withCentre({ point.x, point.y - 16.0f }).constrainedWithin(bounds.reduced(2.0f));
        g.setColour(pal.bgTop.withAlpha(0.92f));
        g.fillRoundedRectangle(box, 3.0f);
        g.setColour(pal.textBright);
        g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        g.drawText(readout, box, juce::Justification::centred);
    }
}

} // namespace openglitch::ui
