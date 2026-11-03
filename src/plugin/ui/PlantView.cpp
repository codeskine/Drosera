#include "PlantView.h"
#include <algorithm>
#include <cmath>
#include "Theme.h"

namespace drosera {

namespace {
constexpr float kRepaintChange = 0.002f;  // of a tentacle's travel
constexpr float kStoppedAfterSeconds = 0.2f;  // longer than an 8192-sample block at 44.1 kHz
constexpr int kSegments = 10;
constexpr float kFeedbackRing = 32.0f;

/// The leaf blade's slightly irregular rim.
float leafRadius(float angle) {
    return 50.0f * (1.0f + 0.05f * std::sin(3.0f * angle + 1.1f) + 0.035f * std::sin(5.0f * angle + 0.4f) + 0.02f * std::sin(8.0f * angle));
}

juce::Colour mixed(juce::Colour a, juce::Colour b, float t) { return a.interpolatedWith(b, t); }
} // namespace

PlantView::PlantView() {
    levels_.fill(kFloorDb);
    targets_.fill(kFloorDb);
    setInterceptsMouseClicks(false, true);  // only the knob at the heart takes clicks
    setOpaque(false);
    feedback_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    feedback_.setRotaryParameters(juce::degreesToRadians(225.0f), juce::degreesToRadians(495.0f), true);
    feedback_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    feedback_.setTitle("Feedback");
    feedback_.getProperties().set("drawnByOwner", true);  // DroseraLookAndFeel::kDrawnByOwner: the plant draws its ring
    feedback_.onValueChange = [this] { repaint(); };
    addAndMakeVisible(feedback_);

    uint32_t seed = 17;  // a fixed seed, so the plant has the same shape every time
    auto next = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / (float) (1u << 24); };
    for (int j = 0; j < kNumBands; ++j) {
        auto& t = tentacles_[(size_t) j];
        t.angle = juce::MathConstants<float>::twoPi * (float) j / (float) kNumBands - juce::MathConstants<float>::halfPi + (next() - 0.5f) * 0.09f;
        t.base = 62.0f + 34.0f * next();
        t.reach = 70.0f + 45.0f * next();
        t.curl = (next() < 0.5f ? -1.0f : 1.0f) * (0.45f + 0.5f * next());
        t.sway = (next() - 0.5f) * 0.25f;
        t.width = 2.1f + 0.9f * next();
        t.drop = 0.7f + 0.6f * next();
    }
}

void PlantView::setVoiceBands(const std::array<int, 3>& bands) {
    if (bands == voiceBands_) return;
    voiceBands_ = bands;
    repaint();
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
    pendingSeconds_ += dtSeconds;
    if (got == 0) {
        // Host blocks can be longer than a tick, so a tick with no audio is not silence. Only a long gap is:
        // a host that stops processing reads as silence.
        if (pendingSeconds_ < kStoppedAfterSeconds) return;
        history_.fill(0.0f);
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
        const float next = smoothLevel(levels_[i], targets_[i], pendingSeconds_);
        levels_[i] = next < kFloorDb + 0.01f ? kFloorDb : next;
        changed |= std::abs(bandAmount(levels_[i]) - drawn_[i]) > kRepaintChange;
    }
    pendingSeconds_ = 0.0f;
    if (changed) repaint();
}

void PlantView::resized() {
    feedback_.setBounds(juce::Rectangle<int>(64, 64).withCentre(getLocalBounds().getCentre()));
}

void PlantView::paint(juce::Graphics& g) {
    const auto centre = getLocalBounds().getCentre().toFloat();
    float mean = 0.0f;
    for (float level : levels_) mean += bandAmount(level);
    mean /= (float) kNumBands;

    for (int j = 0; j < kNumBands; ++j) {
        const auto& t = tentacles_[(size_t) j];
        const int band = tentacleBand(j);
        const float amount = bandAmount(levels_[(size_t) band]);
        drawn_[(size_t) band] = amount;

        // Wider than tall, so the tips stay clear of the header and the row below.
        const float length = (t.base + t.reach * amount) * (1.0f - 0.22f * std::abs(std::sin(t.angle)));
        const float curl = t.curl * (1.0f - 0.8f * amount);
        auto point = centre.getPointOnCircumference(leafRadius(t.angle) - 3.0f, t.angle + juce::MathConstants<float>::halfPi);
        for (int k = 1; k <= kSegments; ++k) {
            const float u = (float) k / (float) kSegments, from = (float) (k - 1) / (float) kSegments;
            const float heading = t.angle + t.sway * (1.0f - u) + curl * u * u;
            const auto next = point + juce::Point<float>(std::cos(heading), std::sin(heading)) * (length / (float) kSegments);
            g.setColour(mixed(juce::Colour(0xff6e1a2a), juce::Colour(0xffd64a62), from).withAlpha(0.7f + 0.3f * amount));
            g.drawLine({point, next}, juce::jmap(from, t.width + 0.8f * amount, 0.7f));
            point = next;
        }

        const float radius = (1.5f + 4.3f * amount) * t.drop;
        for (const float halo : {2.2f, 1.5f}) {  // a glow of translucent rings, cheaper than a blur
            g.setColour(theme::echo().withAlpha(0.1f * amount));
            g.fillEllipse(juce::Rectangle<float>(2.0f * radius * halo, 2.0f * radius * halo).withCentre(point));
        }
        juce::ColourGradient dew(juce::Colour(0xfffff6e4).withAlpha(0.5f + 0.5f * amount), point.translated(-0.35f * radius, -0.35f * radius),
                                 juce::Colour(0xffd6784a).withAlpha(0.35f + 0.4f * amount), point.translated(radius, 0.0f), true);
        dew.addColour(0.45, theme::echo().withAlpha(0.45f + 0.55f * amount));
        g.setGradientFill(dew);
        g.fillEllipse(juce::Rectangle<float>(2.0f * radius, 2.0f * radius).withCentre(point));
        if (isMarked(band)) {
            g.setColour(theme::text());
            g.drawEllipse(juce::Rectangle<float>(2.0f * radius + 6.0f, 2.0f * radius + 6.0f).withCentre(point), 1.0f);
        }
    }

    // The leaf blade: no outline, it fades into the background.
    juce::Path leaf;
    for (int i = 0; i <= 72; ++i) {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 72.0f;
        const auto p = centre + juce::Point<float>(std::cos(a), std::sin(a)) * leafRadius(a);
        i == 0 ? leaf.startNewSubPath(p) : leaf.lineTo(p);
    }
    leaf.closeSubPath();
    juce::ColourGradient blade(juce::Colour::fromRGB((juce::uint8) (110.0f + 40.0f * mean), 30, 48), centre.translated(-10.0f, -12.0f),
                               juce::Colour(0x002c0b14), centre.translated(56.0f, 0.0f), true);
    blade.addColour(0.6, juce::Colour(0xff4d1222));
    g.setGradientFill(blade);
    g.fillPath(leaf);
    g.setColour(juce::Colour(0x59a0384c));
    for (int i = 0; i < 9; ++i) {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 9.0f + 0.3f;
        juce::Path vein;
        vein.startNewSubPath(centre + juce::Point<float>(std::cos(a), std::sin(a)) * 8.0f);
        vein.quadraticTo(centre + juce::Point<float>(std::cos(a + 0.25f), std::sin(a + 0.25f)) * 28.0f,
                         centre + juce::Point<float>(std::cos(a), std::sin(a)) * 46.0f);
        g.strokePath(vein, juce::PathStrokeType(0.8f));
    }

    // Feedback, drawn on the leaf.
    const float start = juce::degreesToRadians(225.0f), end = juce::degreesToRadians(495.0f);
    const float angle = start + (float) feedback_.valueToProportionOfLength(feedback_.getValue()) * (end - start);
    juce::Path track, value;
    track.addCentredArc(centre.x, centre.y, kFeedbackRing, kFeedbackRing, 0.0f, start, end, true);
    value.addCentredArc(centre.x, centre.y, kFeedbackRing, kFeedbackRing, 0.0f, start, angle, true);
    g.setColour(juce::Colour(0x8c14060a));
    g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(theme::loop().withAlpha(0.2f + 0.3f * mean));
    g.strokePath(value, juce::PathStrokeType(9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(juce::Colour(0xffee6a80));
    g.strokePath(value, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(theme::text());
    g.setFont(theme::labelFont().withHeight(10.5f));
    g.drawText("Feedback", juce::Rectangle<float>(70.0f, 14.0f).withCentre(centre.translated(0.0f, -6.0f)), juce::Justification::centred);
    g.setColour(theme::echo());
    g.setFont(theme::labelFont());
    g.drawText(feedbackText_, juce::Rectangle<float>(70.0f, 14.0f).withCentre(centre.translated(0.0f, 9.0f)), juce::Justification::centred);
}

} // namespace drosera
