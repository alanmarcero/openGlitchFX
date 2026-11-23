#include "GlitchLookAndFeel.h"
#include <cmath>
#include <numbers>

namespace openglitch::ui {

namespace {

// Where 0 sits on a slider, so bipolar controls fill from the centre.
float zeroProportion(juce::Slider& slider) {
    return static_cast<float>(slider.valueToProportionOfLength(std::clamp(0.0, slider.getMinimum(), slider.getMaximum())));
}

ThemePalette makePalette(Theme t) {
    switch (t) {
        case Theme::IndustrialAmber:
            return {
                juce::Colour(0xff181410), // bgTop
                juce::Colour(0xff0e0b08), // bgBottom
                juce::Colour(0xd81d1712), // cardBg
                juce::Colour(0xff3d3024), // cardBorder
                juce::Colour(0xffff9d1c), // cardActiveBorder
                juce::Colour(0xff261e16), // headerPlate
                juce::Colour(0xffffa019), // accentPrimary (Warm Amber)
                juce::Colour(0xffff5522), // accentSecondary (Orange Red)
                juce::Colour(0xffff9d1c), // stepActiveFill
                juce::Colour(0xff231d17), // stepInactiveFill
                juce::Colour(0xfffff0b8), // stepPlayheadOutline
                juce::Colour(0xff38302a), // knobBodyTop
                juce::Colour(0xff16120e), // knobBodyBottom
                juce::Colour(0xfffff2df), // textBright
                juce::Colour(0xffb59b7d)  // textMuted
            };

        case Theme::AcidMatrix:
            return {
                juce::Colour(0xff0f1812), // bgTop
                juce::Colour(0xff070d09), // bgBottom
                juce::Colour(0xd8121f16), // cardBg
                juce::Colour(0xff25422e), // cardBorder
                juce::Colour(0xff33ff77), // cardActiveBorder
                juce::Colour(0xff17291d), // headerPlate
                juce::Colour(0xff29ff70), // accentPrimary (Matrix Green)
                juce::Colour(0xffc8ff29), // accentSecondary (Acid Lime)
                juce::Colour(0xff29ff70), // stepActiveFill
                juce::Colour(0xff16241a), // stepInactiveFill
                juce::Colour(0xffffffff), // stepPlayheadOutline
                juce::Colour(0xff26362b), // knobBodyTop
                juce::Colour(0xff0e1610), // knobBodyBottom
                juce::Colour(0xffe6ffed), // textBright
                juce::Colour(0xff7cb88c)  // textMuted
            };

        case Theme::ArcticGlitch:
            return {
                juce::Colour(0xff18202c), // bgTop
                juce::Colour(0xff0f151f), // bgBottom
                juce::Colour(0xd81d2736), // cardBg
                juce::Colour(0xff374a66), // cardBorder
                juce::Colour(0xff5ce1ff), // cardActiveBorder
                juce::Colour(0xff263347), // headerPlate
                juce::Colour(0xff5ce1ff), // accentPrimary (Ice Cyan)
                juce::Colour(0xff8c7aff), // accentSecondary (Ultraviolet)
                juce::Colour(0xff5ce1ff), // stepActiveFill
                juce::Colour(0xff1f2a3a), // stepInactiveFill
                juce::Colour(0xffffffff), // stepPlayheadOutline
                juce::Colour(0xff344357), // knobBodyTop
                juce::Colour(0xff161e29), // knobBodyBottom
                juce::Colour(0xfff0f8ff), // textBright
                juce::Colour(0xff8fa8c7)  // textMuted
            };

        case Theme::CyberNeon:
        default:
            return {
                juce::Colour(0xff152026), // bgTop
                juce::Colour(0xff0d1317), // bgBottom
                juce::Colour(0xef152026), // cardBg
                juce::Colour(0xff283842), // cardBorder
                juce::Colour(0xff48f0c8), // cardActiveBorder
                juce::Colour(0xff1c2b33), // headerPlate
                juce::Colour(0xff48f0c8), // accentPrimary
                juce::Colour(0xffffbe3b), // accentSecondary
                juce::Colour(0xff48f0c8), // stepActiveFill
                juce::Colour(0xff111a1f), // stepInactiveFill
                juce::Colour(0xffe6f0f5), // stepPlayheadOutline
                juce::Colour(0xff283842), // knobBodyTop
                juce::Colour(0xff0d1317), // knobBodyBottom
                juce::Colour(0xffe6f0f5), // textBright
                juce::Colour(0xff869ca8)  // textMuted
            };
    }
}

} // namespace

GlitchLookAndFeel::GlitchLookAndFeel() {
    setTheme(Theme::CyberNeon);
}

void GlitchLookAndFeel::setTheme(Theme theme) {
    currentTheme_ = theme;
    palette_ = makePalette(theme);

    setColour(juce::Slider::textBoxTextColourId, palette_.textBright);
    setColour(juce::Slider::textBoxBackgroundColourId, palette_.bgBottom.withAlpha(0.85f));
    setColour(juce::Slider::textBoxOutlineColourId, palette_.cardBorder);
    setColour(juce::Slider::backgroundColourId, palette_.cardBorder);
    setColour(juce::Slider::trackColourId, palette_.accentPrimary);
    setColour(juce::Slider::thumbColourId, palette_.accentPrimary);
    setColour(juce::ComboBox::backgroundColourId, palette_.headerPlate);
    setColour(juce::ComboBox::textColourId, palette_.textBright);
    setColour(juce::ComboBox::outlineColourId, palette_.cardBorder);
    setColour(juce::PopupMenu::backgroundColourId, palette_.bgTop);
    setColour(juce::PopupMenu::textColourId, palette_.textBright);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, palette_.bgTop.interpolatedWith(palette_.accentPrimary, 0.28f));
    setColour(juce::PopupMenu::highlightedTextColourId, palette_.textBright);
    setColour(juce::TooltipWindow::backgroundColourId, palette_.bgTop);
    setColour(juce::TooltipWindow::textColourId, palette_.textBright);
    setColour(juce::TooltipWindow::outlineColourId, palette_.cardBorder);
    setColour(juce::TextEditor::backgroundColourId, palette_.bgBottom);
    setColour(juce::TextEditor::textColourId, palette_.textBright);
    setColour(juce::TextEditor::focusedOutlineColourId, palette_.accentPrimary);
    setColour(juce::TextButton::buttonColourId, palette_.headerPlate);
    setColour(juce::TextButton::textColourOffId, palette_.textBright);
    setColour(juce::TextButton::textColourOnId, palette_.accentPrimary);
}

void GlitchLookAndFeel::getIdealPopupMenuItemSize(const juce::String& text, bool separator, int height,
                                                  int& idealWidth, int& idealHeight) {
    LookAndFeel_V4::getIdealPopupMenuItemSize(text, separator, height, idealWidth, idealHeight);
    if (!separator) idealHeight = std::max(idealHeight, 36);
}

void GlitchLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                         int x, int y, int width, int height,
                                         float sliderPosProportional,
                                         float rotaryStartAngle,
                                         float rotaryEndAngle,
                                         juce::Slider& slider) {
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                               static_cast<float>(width), static_cast<float>(height)).reduced(4.0f);
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const bool enabled = slider.isEnabled();
    if (!enabled) {
        g.beginTransparencyLayer(0.4f);
    }

    const float cx = bounds.getCentreX();
    const float cy = bounds.getCentreY();
    const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    const float zero = zeroProportion(slider);
    const float zeroAngle = rotaryStartAngle + zero * (rotaryEndAngle - rotaryStartAngle);
    const bool hasValue = std::abs(sliderPosProportional - zero) > 0.005f;

    // 0. Outer Cyber Scale Ticks
    constexpr int kNumTicks = 17;
    for (int i = 0; i < kNumTicks; ++i) {
        const float norm = static_cast<float>(i) / static_cast<float>(kNumTicks - 1);
        const float ta = rotaryStartAngle + norm * (rotaryEndAngle - rotaryStartAngle);
        const bool isMajor = (i % 4 == 0);
        const bool isLit = hasValue && norm >= std::min(zero, sliderPosProportional) - 0.005f
                        && norm <= std::max(zero, sliderPosProportional) + 0.005f;
        const float rIn = radius - 0.5f;
        const float rOut = radius + (isMajor ? 3.5f : 2.0f);

        juce::Colour tickCol = isLit
            ? palette_.accentPrimary.interpolatedWith(palette_.accentSecondary, norm)
            : palette_.textMuted.withAlpha(isMajor ? 0.50f : 0.25f);
        g.setColour(tickCol);
        g.drawLine(cx + std::sin(ta) * rIn, cy - std::cos(ta) * rIn,
                   cx + std::sin(ta) * rOut, cy - std::cos(ta) * rOut,
                   isMajor ? 1.4f : 0.9f);
    }

    // 1. Outer LED Value Arc Track
    const float arcRadius = radius - 3.0f;
    juce::Path bgArc;
    bgArc.addCentredArc(cx, cy, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(palette_.stepInactiveFill.brighter(0.15f));
    g.strokePath(bgArc, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (hasValue) {
        juce::Path valArc;
        valArc.addCentredArc(cx, cy, arcRadius, arcRadius, 0.0f, std::min(zeroAngle, angle), std::max(zeroAngle, angle), true);

        juce::ColourGradient arcGrad(palette_.accentPrimary, cx - arcRadius, cy,
                                     palette_.accentSecondary, cx + arcRadius, cy, false);
        g.setGradientFill(arcGrad);
        g.strokePath(valArc, juce::PathStrokeType(3.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Subtle outer glow
        g.setColour(palette_.accentPrimary.withAlpha(0.24f));
        g.strokePath(valArc, juce::PathStrokeType(7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // 2. Knurled Anodized Knob Body
    const float bodyRadius = radius * 0.72f;
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillEllipse(cx - bodyRadius + 1.0f, cy - bodyRadius + 2.5f, bodyRadius * 2.0f, bodyRadius * 2.0f);

    juce::ColourGradient bodyGrad(palette_.knobBodyTop, cx, cy - bodyRadius,
                                  palette_.knobBodyBottom, cx, cy + bodyRadius, false);
    g.setGradientFill(bodyGrad);
    g.fillEllipse(cx - bodyRadius, cy - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f);

    // Knurled rim ticks
    g.setColour(palette_.cardBorder.brighter(0.2f));
    g.drawEllipse(cx - bodyRadius, cy - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f, 1.3f);

    const int numKnurls = 24;
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    for (int i = 0; i < numKnurls; ++i) {
        const float ka = static_cast<float>(i) * (2.0f * std::numbers::pi_v<float> / static_cast<float>(numKnurls));
        const float r0 = bodyRadius * 0.85f;
        const float r1 = bodyRadius * 0.98f;
        g.drawLine(cx + std::sin(ka) * r0, cy - std::cos(ka) * r0,
                   cx + std::sin(ka) * r1, cy - std::cos(ka) * r1, 1.0f);
    }

    // Inner cap with subtle neon rim
    const float capRadius = bodyRadius * 0.72f;
    juce::ColourGradient capGrad(palette_.knobBodyBottom.brighter(0.12f), cx, cy - capRadius,
                                 palette_.knobBodyBottom.darker(0.2f), cx, cy + capRadius, false);
    g.setGradientFill(capGrad);
    g.fillEllipse(cx - capRadius, cy - capRadius, capRadius * 2.0f, capRadius * 2.0f);

    g.setColour(palette_.accentPrimary.withAlpha(0.32f));
    g.drawEllipse(cx - capRadius + 0.5f, cy - capRadius + 0.5f, capRadius * 2.0f - 1.0f, capRadius * 2.0f - 1.0f, 0.9f);

    // 3. Illuminated Pointer Line & Tip Dot
    const float p0 = capRadius * 0.25f;
    const float p1 = bodyRadius * 0.88f;
    const float sx0 = cx + std::sin(angle) * p0;
    const float sy0 = cy - std::cos(angle) * p0;
    const float sx1 = cx + std::sin(angle) * p1;
    const float sy1 = cy - std::cos(angle) * p1;

    g.setColour(palette_.accentPrimary.withAlpha(0.45f));
    g.drawLine(sx0, sy0, sx1, sy1, 4.0f);
    g.setColour(palette_.textBright);
    g.drawLine(sx0, sy0, sx1, sy1, 2.0f);
    g.setColour(palette_.accentPrimary);
    g.fillEllipse(sx1 - 2.2f, sy1 - 2.2f, 4.4f, 4.4f);

    if (!enabled) {
        g.endTransparencyLayer();
    }
}

void GlitchLookAndFeel::drawLinearSlider(juce::Graphics& g,
                                         int x, int y, int width, int height,
                                         float sliderPos, float minSliderPos, float maxSliderPos,
                                         juce::Slider::SliderStyle style,
                                         juce::Slider& slider) {
    const bool enabled = slider.isEnabled();
    if (!enabled) {
        g.beginTransparencyLayer(0.4f);
    }
    if (!slider.isBar()) {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        if (!enabled) {
            g.endTransparencyLayer();
        }
        return;
    }

    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                               static_cast<float>(width), static_cast<float>(height));
    const auto inner = bounds.reduced(1.0f);
    const float zeroX = static_cast<float>(x) + static_cast<float>(width) * zeroProportion(slider);
    const float fillLeft = std::clamp(std::min(zeroX, sliderPos), inner.getX(), inner.getRight());
    const float fillRight = std::clamp(std::max(zeroX, sliderPos), inner.getX(), inner.getRight());
    const auto trackCol = slider.findColour(juce::Slider::trackColourId);

    // Subtle body fill + center scrim + bottom-edge bar keep centered text readable without a harsh vertical bisection.
    g.setColour(trackCol.withAlpha(0.18f));
    g.fillRoundedRectangle(juce::Rectangle<float>::leftTopRightBottom(fillLeft, inner.getY(), fillRight, inner.getBottom()), 3.5f);

    if (inner.getWidth() > 28.0f) {
        g.setColour(palette_.bgBottom.withAlpha(0.55f));
        g.fillRoundedRectangle(inner.reduced(12.0f, 5.0f), 3.0f);
    }

    g.setColour(trackCol.withAlpha(0.90f));
    g.fillRoundedRectangle(juce::Rectangle<float>::leftTopRightBottom(fillLeft, inner.getBottom() - 3.0f, fillRight, inner.getBottom()), 1.5f);

    g.setColour(slider.hasKeyboardFocus(true) ? palette_.accentPrimary : palette_.cardBorder.brighter(0.25f));
    g.drawRoundedRectangle(inner, 4.0f, 1.0f);

    if (!enabled) {
        g.endTransparencyLayer();
    }
}

void GlitchLookAndFeel::drawToggleButton(juce::Graphics& g,
                                         juce::ToggleButton& button,
                                         bool shouldDrawButtonAsHighlighted,
                                         bool /*shouldDrawButtonAsDown*/) {
    const bool enabled = button.isEnabled();
    if (!enabled) {
        g.beginTransparencyLayer(0.4f);
    }
    const auto bounds = button.getLocalBounds().toFloat().reduced(2.0f);
    const bool on = button.getToggleState();

    juce::Colour bg = on ? palette_.accentPrimary.withAlpha(0.22f) : palette_.stepInactiveFill;
    if (enabled && shouldDrawButtonAsHighlighted) {
        bg = bg.brighter(0.15f);
    }

    g.setColour(bg);
    g.fillRoundedRectangle(bounds, 4.0f);

    g.setColour(on || button.hasKeyboardFocus(false) ? palette_.accentPrimary : palette_.cardBorder);
    g.drawRoundedRectangle(bounds, 4.0f, on ? 1.5f : 1.0f);

    // LED Indicator Dot on left
    const float dotR = 3.5f;
    const float dotX = bounds.getX() + 10.0f;
    const float dotY = bounds.getCentreY();
    if (on) {
        g.setColour(palette_.accentPrimary.withAlpha(0.35f));
        g.fillEllipse(dotX - dotR * 1.8f, dotY - dotR * 1.8f, dotR * 3.6f, dotR * 3.6f);
        g.setColour(palette_.accentPrimary);
    } else {
        g.setColour(palette_.textMuted.withAlpha(0.35f));
    }
    g.fillEllipse(dotX - dotR, dotY - dotR, dotR * 2.0f, dotR * 2.0f);

    g.setColour(on ? palette_.textBright : palette_.textMuted);
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    auto textRect = bounds.withTrimmedLeft(18.0f);
    auto text = button.getButtonText();
    if (!on && text == "ON") text = "OFF";
    if (!on && text == "MASTER ON") text = "MASTER OFF";
    g.drawText(text, textRect.toNearestInt(), juce::Justification::centred);
    if (!enabled) {
        g.endTransparencyLayer();
    }
}

void GlitchLookAndFeel::drawButtonBackground(juce::Graphics& g,
                                             juce::Button& button,
                                             const juce::Colour& /*backgroundColour*/,
                                             bool shouldDrawButtonAsHighlighted,
                                             bool shouldDrawButtonAsDown) {
    const float alpha = button.isEnabled() ? 1.0f : 0.4f;
    const auto bounds = button.getLocalBounds().toFloat().reduced(1.5f);
    juce::Colour bg = button.getToggleState() ? palette_.bgTop.interpolatedWith(palette_.accentPrimary, 0.26f) : palette_.headerPlate;
    if (button.isEnabled() && shouldDrawButtonAsDown) {
        bg = palette_.accentPrimary.withAlpha(0.35f);
    } else if (button.isEnabled() && shouldDrawButtonAsHighlighted) {
        bg = palette_.headerPlate.brighter(0.2f);
    }

    g.setColour(bg.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(bounds, 3.5f);

    const bool lit = button.isEnabled() && (shouldDrawButtonAsHighlighted || button.getToggleState() || button.hasKeyboardFocus(false));
    g.setColour((lit ? palette_.accentPrimary : palette_.cardBorder.brighter(0.35f)).withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(bounds, 3.5f, lit ? 1.4f : 1.15f);
}

void GlitchLookAndFeel::drawButtonText(juce::Graphics& g,
                                       juce::TextButton& button,
                                       bool,
                                       bool) {
    const bool on = button.getToggleState();
    const auto colourId = on ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId;
    g.setColour(button.findColour(colourId).withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.4f));
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(3, 0), juce::Justification::centred, 1, 0.82f);
}

void GlitchLookAndFeel::drawComboBox(juce::Graphics& g,
                                     int width, int height,
                                     bool /*isButtonDown*/,
                                     int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                                     juce::ComboBox& box) {
    const float alpha = box.isEnabled() ? 1.0f : 0.4f;
    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(1.0f);
    g.setColour(palette_.headerPlate.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour((box.isEnabled() && box.hasKeyboardFocus(true) ? palette_.accentPrimary : palette_.cardBorder).withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

    // Arrow triangle
    juce::Path arrow;
    const float ax = static_cast<float>(width) - 14.0f;
    const float ay = static_cast<float>(height) * 0.5f;
    arrow.addTriangle(ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour(palette_.accentPrimary.withMultipliedAlpha(alpha));
    g.fillPath(arrow);
}

void GlitchLookAndFeel::drawRackScrew(juce::Graphics& g, float cx, float cy, float radius) const {
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillEllipse(cx - radius, cy - radius + 1.0f, radius * 2.0f, radius * 2.0f);

    juce::ColourGradient grad(palette_.knobBodyTop, cx - radius * 0.5f, cy - radius * 0.5f,
                              palette_.knobBodyBottom, cx + radius * 0.5f, cy + radius * 0.5f, false);
    g.setGradientFill(grad);
    g.fillEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

    g.setColour(palette_.cardBorder);
    g.drawEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 1.0f);

    // Hex / slot socket
    g.setColour(juce::Colours::black.withAlpha(0.75f));
    g.drawLine(cx - radius * 0.5f, cy - radius * 0.25f, cx + radius * 0.5f, cy + radius * 0.25f, 1.6f);
    g.drawLine(cx - radius * 0.25f, cy + radius * 0.5f, cx + radius * 0.25f, cy - radius * 0.5f, 1.6f);
}

} // namespace openglitch::ui
