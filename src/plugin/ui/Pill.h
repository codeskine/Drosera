#pragma once
// The pill-shaped pieces: the CLASSIC / PONG switch, and the cartouche that names a group of controls.

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

namespace drosera {

/// Echo Pong as a two-part pill: CLASSIC on the left, PONG on the right. A click switches it.
class ModeSwitch final : public juce::Button {
public:
    ModeSwitch() : juce::Button("Echo Pong") {
        setClickingTogglesState(true);
        setTitle("Echo Pong");
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool) override {
        const auto r = getLocalBounds().toFloat().reduced(0.5f);
        const bool pong = getToggleState();
        g.setColour(juce::Colour(0xff1e1618));
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        g.setColour(highlighted ? theme::cartouche() : theme::hairline());
        g.drawRoundedRectangle(r, r.getHeight() * 0.5f, 1.0f);

        const auto half = r.reduced(2.0f).withWidth(r.getWidth() * 0.5f - 3.0f);
        const auto knob = pong ? half.withX(r.getCentreX() + 1.0f) : half;
        g.setColour(pong ? juce::Colour(0xff5a3f1e) : juce::Colour(0xff4a1d27));
        g.fillRoundedRectangle(knob, knob.getHeight() * 0.5f);

        g.setFont(theme::tagFont());
        g.setColour(pong ? theme::textDim() : theme::text());
        g.drawText("CLASSIC", r.withWidth(r.getWidth() * 0.5f), juce::Justification::centred);
        g.setColour(pong ? theme::echo() : theme::textDim());
        g.drawText("PONG", r.withTrimmedLeft(r.getWidth() * 0.5f), juce::Justification::centred);
    }
};

/// A hairline pill around the group's name, with fine rules out to the group's edges.
inline void drawCartouche(juce::Graphics& g, const juce::String& name, float x0, float x1, float y, float alpha = 1.0f) {
    const auto font = theme::captionFont();
    const float width = juce::GlyphArrangement::getStringWidth(font, name) + 22.0f, height = 18.0f, centre = 0.5f * (x0 + x1);
    const auto pill = juce::Rectangle<float>(width, height).withCentre({centre, y});
    g.setColour(theme::cartouche().withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(pill, height * 0.5f, 1.0f);
    g.setColour(juce::Colour(0xff2a2022).withMultipliedAlpha(alpha));
    g.drawHorizontalLine(juce::roundToInt(y), x0, pill.getX() - 6.0f);
    g.drawHorizontalLine(juce::roundToInt(y), pill.getRight() + 6.0f, x1);
    g.setFont(font);
    g.setColour(theme::textDim().withMultipliedAlpha(alpha));
    g.drawText(name, pill.translated(1.5f, 0.5f), juce::Justification::centred);  // the letter spacing trails the last letter
}

} // namespace drosera
