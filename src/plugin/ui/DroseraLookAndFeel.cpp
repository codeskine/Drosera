#include "DroseraLookAndFeel.h"
#include "Theme.h"

namespace drosera {

DroseraLookAndFeel::DroseraLookAndFeel() {
    setColour(juce::Label::textColourId, theme::text());
    setColour(juce::ComboBox::textColourId, theme::text());
    setColour(juce::ComboBox::arrowColourId, theme::dew());
    setColour(juce::PopupMenu::backgroundColourId, theme::panel());
    setColour(juce::PopupMenu::textColourId, theme::text());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, theme::track());
    setColour(juce::PopupMenu::highlightedTextColourId, theme::dew());
    setColour(juce::TextButton::textColourOffId, theme::textDim());
    setColour(juce::TextButton::textColourOnId, theme::dew());
}

void DroseraLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float startAngle, float endAngle, juce::Slider& s) {
    const bool main = s.getProperties().contains(kMainControl);
    const float stroke = main ? 3.5f : 2.5f;
    const auto colour = main ? theme::dew() : theme::knob();
    const auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(stroke);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const juce::PathStrokeType arc(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path trackPath;
    trackPath.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(theme::track());
    g.strokePath(trackPath, arc);

    juce::Path valuePath;
    valuePath.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
    g.setColour(colour);
    g.strokePath(valuePath, arc);

    const auto tip = centre.getPointOnCircumference(radius * 0.95f, angle);
    const auto root = centre.getPointOnCircumference(radius * 0.6f, angle);
    g.drawLine({root, tip}, 2.0f);
}

void DroseraLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) {
    const auto r = b.getLocalBounds().toFloat().reduced(1.0f);
    const bool on = b.getToggleState();
    if (on) {
        g.setColour(theme::dew());
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
    }
    g.setColour(on ? theme::dew() : (highlighted ? theme::knob() : theme::track()));
    g.drawRoundedRectangle(r, r.getHeight() * 0.5f, 1.0f);
    g.setColour(on ? theme::background() : theme::text());
    g.setFont(theme::valueFont());
    g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

void DroseraLookAndFeel::drawComboBox(juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) {}

void DroseraLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(box.getLocalBounds());
    label.setFont(theme::labelFont());
    label.setJustificationType(juce::Justification::centred);
}

} // namespace drosera
