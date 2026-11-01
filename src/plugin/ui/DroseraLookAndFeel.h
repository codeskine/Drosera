#pragma once
// Knob, switch and preset selector drawing.

#include <juce_gui_basics/juce_gui_basics.h>

namespace drosera {

class DroseraLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    /// A slider with this property set is drawn as a main control, larger and in the dew colour.
    static constexpr const char* kMainControl = "mainControl";

    DroseraLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos,
                          float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override {}
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
};

} // namespace drosera
