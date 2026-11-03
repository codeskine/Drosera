#include "DroseraLookAndFeel.h"
#include "Theme.h"

namespace drosera {

DroseraLookAndFeel::DroseraLookAndFeel() {
    setColour(juce::Label::textColourId, theme::text());
    setColour(juce::ComboBox::textColourId, theme::text());
    setColour(juce::ComboBox::arrowColourId, theme::echo());
    setColour(juce::PopupMenu::backgroundColourId, theme::panel());
    setColour(juce::PopupMenu::textColourId, theme::text());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, theme::track());
    setColour(juce::PopupMenu::highlightedTextColourId, theme::echo());
    setColour(juce::TextButton::textColourOffId, theme::textDim());
    setColour(juce::TextButton::textColourOnId, theme::echo());
}

void DroseraLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float startAngle, float endAngle, juce::Slider& s) {
    const bool main = s.getProperties().contains(kMainControl);
    const auto colour = s.getProperties()[kGroup].toString() == "loop" ? theme::loop() : theme::echo();
    const float stroke = main ? 4.0f : 2.6f;
    const auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat();
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - (main ? 16.0f : 4.0f);
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);

    if (main) {  // a fine scale outside the arc, every fifth tick longer
        for (int i = 0; i <= 20; ++i) {
            const float a = startAngle + (endAngle - startAngle) * (float) i / 20.0f;
            const float outer = radius + (i % 5 == 0 ? 15.0f : 12.0f);
            g.setColour((float) i / 20.0f <= pos ? colour.withAlpha(0.67f) : juce::Colour(0xff3a2c2e));
            g.drawLine({centre.getPointOnCircumference(radius + 9.0f, a), centre.getPointOnCircumference(outer, a)}, 1.2f);
        }
    }

    const juce::PathStrokeType arc(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path trackPath;
    trackPath.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(theme::track());
    g.strokePath(trackPath, arc);

    juce::Path valuePath;
    valuePath.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
    g.setColour(colour.withAlpha(0.18f));  // a soft glow: a wider stroke at low alpha, cheaper than a blur
    g.strokePath(valuePath, juce::PathStrokeType(stroke * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(colour);
    g.strokePath(valuePath, arc);

    const float capRadius = radius - stroke - 3.0f;
    const auto cap = juce::Rectangle<float>(2.0f * capRadius, 2.0f * capRadius).withCentre(centre);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2f2426), centre.translated(-0.3f * radius, -0.35f * radius),
                                           juce::Colour(0xff151012), centre.translated(0.0f, 0.82f * radius), true));
    g.fillEllipse(cap);
    g.setColour(theme::hairline());
    g.drawEllipse(cap, 1.0f);

    g.setColour(theme::text());
    g.drawLine({centre.getPointOnCircumference(radius * 0.32f, angle), centre.getPointOnCircumference(radius - stroke - 7.0f, angle)},
               main ? 2.4f : 1.8f);
}

void DroseraLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) {
    const auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = b.getToggleState(), echo = b.getProperties()[kGroup].toString() == "echo";
    g.setColour(on ? juce::Colour(echo ? 0xff5a3f1e : 0xff4a1d27) : juce::Colour(0xff1e1618));
    g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
    g.setColour(on ? (echo ? theme::echo() : theme::loop()) : (highlighted ? theme::cartouche() : theme::hairline()));
    g.drawRoundedRectangle(r, r.getHeight() * 0.5f, 1.0f);
    g.setColour(on ? (echo ? theme::echo() : theme::text()) : theme::textDim());
    g.setFont(theme::tagFont());
    g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

void DroseraLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool highlighted, bool) {
    g.setColour(b.getToggleState() ? theme::echo() : (highlighted ? theme::text() : theme::textDim()));
    g.setFont(theme::captionFont());
    g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

void DroseraLookAndFeel::drawComboBox(juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) {}

void DroseraLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(box.getLocalBounds());
    label.setFont(theme::labelFont());
    label.setJustificationType(juce::Justification::centred);
}

} // namespace drosera
