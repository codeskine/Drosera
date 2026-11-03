#include "ControlRow.h"
#include <cmath>
#include "DroseraLookAndFeel.h"
#include "Theme.h"

namespace drosera {

namespace {
struct Place { const char* id; const char* caption; int x; KnobSize size; KnobGroup group; };

// Centres in the window, from the mockup. The knobs' centres sit on y 466.
constexpr int kKnobY = 466, kCellWidth = 58, kCellHeight = 92;
const Place kPlaces[] = {
    {"hpf", "Highpass", 66, KnobSize::small, KnobGroup::loop},  {"lpf", "Lowpass", 126, KnobSize::small, KnobGroup::loop},
    {"revmix", "Mix", 206, KnobSize::small, KnobGroup::loop},   {"revdecay", "Decay", 266, KnobSize::small, KnobGroup::loop},
    {"echodrive", "Drive", 360, KnobSize::mini, KnobGroup::echo}, {"echotone", "Tone", 420, KnobSize::mini, KnobGroup::echo},
    {"echowidth", "Width", 480, KnobSize::mini, KnobGroup::echo}, {"echolevel", "Level", 540, KnobSize::mini, KnobGroup::echo},
    {"echosend", "Send", 634, KnobSize::small, KnobGroup::echo}, {"echotime", "Time", 694, KnobSize::small, KnobGroup::echo},
    {"echofb", "Feedback", 754, KnobSize::small, KnobGroup::echo},
};
constexpr const char* kPongKnobs[] = {"echodrive", "echotone", "echowidth", "echolevel"};

juce::RangedAudioParameter& parameter(juce::AudioProcessorValueTreeState& apvts, const char* id) {
    auto* p = apvts.getParameter(id);
    jassert(p != nullptr);
    return *p;
}

/// In the row's coordinates, from the window's.
float rowY(float windowY) { return windowY - (float) ControlRow::kTop; }
} // namespace

ControlRow::ControlRow(juce::AudioProcessorValueTreeState& apvts)
    : modeAttachment_(parameter(apvts, "echopong"), mode_, nullptr),
      warpAttachment_(parameter(apvts, "echohalve"), warp_, nullptr) {
    for (const auto& place : kPlaces) {
        knobs_[place.id] = std::make_unique<LabeledKnob>(parameter(apvts, place.id), place.caption, place.size, place.group);
        addAndMakeVisible(*knobs_[place.id]);
    }
    warp_.setClickingTogglesState(true);
    warp_.setTitle("Echo Warp");
    warp_.getProperties().set(DroseraLookAndFeel::kGroup, "echo");
    addAndMakeVisible(mode_);
    addAndMakeVisible(warp_);
    arcs::bandArc(10.0f, 18000.0f, bandTicks_);
    arcs::toneArc(800.0f, toneTicks_);
}

juce::Button* ControlRow::toggle(const juce::String& paramId) {
    if (paramId == "echopong") return &mode_;
    if (paramId == "echohalve") return &warp_;
    return nullptr;
}

void ControlRow::refresh(float highpassHz, float lowpassHz, float toneHz, bool pong, pulse::Lit lit) {
    for (const char* id : kPongKnobs) knobs_[id]->setDimmed(!pong);  // setAlpha repaints only on a change
    float band[arcs::kBandTicks], tone[arcs::kToneTicks];
    arcs::bandArc(highpassHz, lowpassHz, band);
    arcs::toneArc(toneHz, tone);
    const bool changed = pong != pong_ || lit != lit_ || !std::equal(band, band + arcs::kBandTicks, bandTicks_)
                         || !std::equal(tone, tone + arcs::kToneTicks, toneTicks_);
    if (!changed) return;
    std::copy(band, band + arcs::kBandTicks, bandTicks_);
    std::copy(tone, tone + arcs::kToneTicks, toneTicks_);
    pong_ = pong;
    lit_ = lit;
    repaint();
}

void ControlRow::paint(juce::Graphics& g) {
    // The band arc over Highpass and Lowpass: ticks in the band that survives are full red.
    for (int i = 0; i < arcs::kBandTicks; ++i) {
        const float x = 46.0f + 100.0f * (float) i / (float) (arcs::kBandTicks - 1), base = rowY(430.0f);
        const bool passes = bandTicks_[i] > arcs::kBandStub + 0.5f * arcs::kBandSpan;
        g.setColour(passes ? theme::loop() : juce::Colour(0xff6d2a37));
        g.drawLine(x, base, x, base - bandTicks_[i], 1.5f);
    }
    // The Tone arc, dimmed with PONG's knobs.
    g.setColour(theme::echo().withAlpha(pong_ ? 1.0f : 0.35f));
    for (int i = 0; i < arcs::kToneTicks; ++i) {
        const float x = 404.0f + 32.0f * (float) i / (float) (arcs::kToneTicks - 1), base = rowY(432.0f);
        g.drawLine(x, base, x, base - toneTicks_[i], 1.4f);
    }
    // The echo pulse beside Time: one dot, or two that take turns with PONG.
    auto dot = [&](float x, bool on) {
        g.setColour(theme::echo().withAlpha(on ? 1.0f : 0.2f));
        g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre({x, rowY(452.0f)}));
    };
    if (pong_) {
        dot(716.0f, lit_ == pulse::Lit::left);
        dot(726.0f, lit_ == pulse::Lit::right);
    } else {
        dot(720.0f, lit_ == pulse::Lit::single);
    }

    drawCartouche(g, "FILTER", 44.0f, 148.0f, rowY(534.0f));
    drawCartouche(g, "REVERB", 184.0f, 288.0f, rowY(534.0f));
    drawCartouche(g, "PONG", 340.0f, 560.0f, rowY(534.0f), pong_ ? 1.0f : 0.4f);
    drawCartouche(g, "ECHO", 612.0f, 866.0f, rowY(534.0f));
}

void ControlRow::resized() {
    for (const auto& place : kPlaces) {
        auto& knob = *knobs_[place.id];
        const int top = kKnobY - kTop - LabeledKnob::radius(place.size) - LabeledKnob::margin(place.size);
        knob.setBounds(place.x - kCellWidth / 2, top, kCellWidth, kCellHeight);
    }
    mode_.setBounds(juce::Rectangle<int>(92, 22).withCentre({834, 458 - kTop}));
    warp_.setBounds(juce::Rectangle<int>(50, 18).withCentre({834, 490 - kTop}));
}

} // namespace drosera
