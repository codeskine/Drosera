#include "LabeledKnob.h"
#include "DroseraLookAndFeel.h"
#include "Theme.h"

namespace drosera {

namespace {
juce::Label& style(juce::Label& label, const juce::Font& font, juce::Colour colour) {
    label.setFont(font);
    label.setColour(juce::Label::textColourId, colour);
    label.setJustificationType(juce::Justification::centred);
    label.setInterceptsMouseClicks(false, false);
    label.setBorderSize({});
    return label;
}
} // namespace

LabeledKnob::LabeledKnob(juce::RangedAudioParameter& parameter, const juce::String& caption, KnobSize size, KnobGroup group)
    : parameter_(parameter), size_(size), attachment_(parameter, slider_, nullptr) {
    slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider_.setRotaryParameters(juce::degreesToRadians(225.0f), juce::degreesToRadians(495.0f), true);
    slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider_.setTitle(caption);
    if (size == KnobSize::large) slider_.getProperties().set(DroseraLookAndFeel::kMainControl, true);
    slider_.getProperties().set(DroseraLookAndFeel::kGroup, group == KnobGroup::loop ? "loop" : "echo");
    addAndMakeVisible(slider_);

    const bool large = size == KnobSize::large;
    caption_.setText(caption, juce::dontSendNotification);
    addAndMakeVisible(style(caption_, large ? theme::largeLabelFont() : theme::labelFont(), theme::text()));
    addAndMakeVisible(style(value_, large ? theme::largeValueFont() : theme::valueFont(), theme::textDim()));
    addAndMakeVisible(style(tag_, theme::tagFont(), theme::echo()));
}

void LabeledKnob::show(const juce::String& value, const juce::String& tag) {
    value_.setText(value, juce::dontSendNotification);
    tag_.setText(tag, juce::dontSendNotification);
    slider_.setAlpha(tag.isEmpty() ? 1.0f : 0.45f);
}

void LabeledKnob::setDimmed(bool dimmed) {
    setAlpha(dimmed ? 0.35f : 1.0f);
}

juce::Point<int> LabeledKnob::knobCentre() const {
    return { getWidth() / 2, radius(size_) + margin(size_) };
}

void LabeledKnob::resized() {
    const bool large = size_ == KnobSize::large;
    const int side = 2 * (radius(size_) + margin(size_));
    auto r = getLocalBounds();
    slider_.setBounds(r.removeFromTop(side).withSizeKeepingCentre(side, side));
    if (large) r.removeFromTop(6);
    caption_.setBounds(r.removeFromTop(large ? 18 : 15));
    value_.setBounds(r.removeFromTop(large ? 16 : 13));
    tag_.setBounds(r.removeFromTop(13));
}

} // namespace drosera
