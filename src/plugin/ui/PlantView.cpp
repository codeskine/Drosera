#include "PlantView.h"
#include <algorithm>
#include <cmath>
#include "Theme.h"

namespace drosera {

namespace {
// In the component's coordinates, which are the window's.
const juce::Point<float> kCentre { 380.0f, 192.0f };
constexpr float kInner = 54.0f, kReach = 88.0f;
constexpr float kRepaintChange = 0.002f;  // of a tentacle's travel
} // namespace

PlantView::PlantView() {
    levels_.fill(kFloorDb);
    targets_.fill(kFloorDb);
    setInterceptsMouseClicks(false, true);  // only the knob at the heart takes clicks
    setOpaque(false);
    feedback_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    feedback_.setRotaryParameters(juce::degreesToRadians(225.0f), juce::degreesToRadians(495.0f), true);
    feedback_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    feedback_.getProperties().set("mainControl", true);  // DroseraLookAndFeel::kMainControl
    addAndMakeVisible(feedback_);
}

void PlantView::setFeedbackText(const juce::String& text) {
    if (text != feedbackText_) {
        feedbackText_ = text;
        repaint();
    }
}

void PlantView::update(SpectrumTap& tap, double sampleRate, float dtSeconds) {
    std::array<float, SpectrumTap::kCapacity> incoming;
    const size_t got = tap.pop(incoming.data(), incoming.size());
    if (got == 0) {
        history_.fill(0.0f);  // a host that stops processing reads as silence
    } else if (got >= history_.size()) {
        std::copy(incoming.begin() + (std::ptrdiff_t) (got - history_.size()), incoming.begin() + (std::ptrdiff_t) got, history_.begin());
    } else {
        std::move(history_.begin() + (std::ptrdiff_t) got, history_.end(), history_.begin());
        std::copy_n(incoming.begin(), got, history_.end() - (std::ptrdiff_t) got);
    }

    std::copy(history_.begin(), history_.end(), fftData_.begin());
    std::fill(fftData_.begin() + kFftSize, fftData_.end(), 0.0f);
    window_.multiplyWithWindowingTable(fftData_.data(), (size_t) kFftSize);
    fft_.performFrequencyOnlyForwardTransform(fftData_.data(), true);
    computeBands(fftData_.data(), kFftSize, sampleRate, targets_.data());

    bool changed = false;
    for (size_t i = 0; i < levels_.size(); ++i) {
        // Snap at the end of a release, so a silent plant comes truly to rest.
        const float next = smoothLevel(levels_[i], targets_[i], dtSeconds);
        levels_[i] = next < kFloorDb + 0.01f ? kFloorDb : next;
        changed |= std::abs(bandAmount(levels_[i]) - drawn_[i]) > kRepaintChange;
    }
    if (changed) repaint();
}

void PlantView::resized() {
    const float size = 2.0f * (kInner - 12.0f);
    feedback_.setBounds(juce::Rectangle<float>(size, size).withCentre(kCentre).toNearestInt());
}

void PlantView::paint(juce::Graphics& g) {
    const float glow = kInner + kReach + 22.0f;
    g.setGradientFill(juce::ColourGradient(theme::heart().withAlpha(0.75f), kCentre,
                                           theme::background().withAlpha(0.0f), kCentre.translated(glow, 0.0f), true));
    g.fillEllipse(juce::Rectangle<float>(2.0f * glow, 2.0f * glow).withCentre(kCentre));

    for (int i = 0; i < kNumBands; ++i) {
        const float amount = bandAmount(levels_[(size_t) i]);
        drawn_[(size_t) i] = amount;
        const float angle = juce::MathConstants<float>::twoPi * (float) i / (float) kNumBands;  // lowest band at the top
        const auto root = kCentre.getPointOnCircumference(kInner + 3.0f, angle);
        const auto tip = kCentre.getPointOnCircumference(kInner + 8.0f + kReach * amount, angle);
        g.setColour(theme::stalk());
        g.drawLine({root, tip}, 1.4f);
        const float radius = 1.8f + 3.4f * amount;
        g.setColour(theme::dew().withAlpha(0.35f + 0.65f * amount));
        g.fillEllipse(juce::Rectangle<float>(2.0f * radius, 2.0f * radius).withCentre(tip));
    }

    const auto disc = juce::Rectangle<float>(2.0f * kInner, 2.0f * kInner).withCentre(kCentre);
    g.setColour(theme::panel());
    g.fillEllipse(disc);
    g.setColour(theme::track());
    g.drawEllipse(disc, 1.5f);

    g.setColour(theme::text());
    g.setFont(theme::tagFont());
    g.drawText("FEEDBACK", juce::Rectangle<float>(80.0f, 14.0f).withCentre(kCentre.translated(0.0f, -8.0f)), juce::Justification::centred);
    g.setColour(theme::dew());
    g.setFont(juce::FontOptions(15.0f));
    g.drawText(feedbackText_, juce::Rectangle<float>(70.0f, 22.0f).withCentre(kCentre.translated(0.0f, 10.0f)), juce::Justification::centred);
}

} // namespace drosera
