#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace openglitch::ui {

enum class Theme : int {
    CyberNeon = 0,
    IndustrialAmber = 1,
    AcidMatrix = 2,
    ArcticGlitch = 3
};

struct ThemePalette {
    juce::Colour bgTop;
    juce::Colour bgBottom;
    juce::Colour cardBg;
    juce::Colour cardBorder;
    juce::Colour cardActiveBorder;
    juce::Colour headerPlate;
    juce::Colour accentPrimary;
    juce::Colour accentSecondary;
    juce::Colour stepActiveFill;
    juce::Colour stepInactiveFill;
    juce::Colour stepPlayheadOutline;
    juce::Colour knobBodyTop;
    juce::Colour knobBodyBottom;
    juce::Colour textBright;
    juce::Colour textMuted;
};

class GlitchLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    GlitchLookAndFeel();

    void setTheme(Theme theme);
    Theme getTheme() const { return currentTheme_; }
    const ThemePalette& getPalette() const { return palette_; }

    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPosProportional,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override;

    void drawToggleButton(juce::Graphics& g,
                          juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    void drawComboBox(juce::Graphics& g,
                      int width, int height,
                      bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;

    void drawRackScrew(juce::Graphics& g, float cx, float cy, float radius) const;

private:
    Theme currentTheme_ = Theme::CyberNeon;
    ThemePalette palette_;
};

} // namespace openglitch::ui
