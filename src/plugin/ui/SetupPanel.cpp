#include "SetupPanel.h"
#include "DroseraLookAndFeel.h"
#include "Pill.h"
#include "Theme.h"

namespace drosera {

namespace {
struct KnobPlace { const char* id; const char* caption; int x; KnobGroup group; };
struct TogglePlace { const char* id; const char* caption; const char* title; int x, y; KnobGroup group; };

// Centres in the window, from the mockup.
constexpr int kKnobY = 476, kCellWidth = 66, kCellHeight = 90;
const KnobPlace kKnobs[] = {
    {"coupling", "Coupling", 140, KnobGroup::loop}, {"interval2", "Interval 2", 210, KnobGroup::loop},
    {"interval3", "Interval 3", 280, KnobGroup::loop}, {"volume", "Volume", 415, KnobGroup::echo},
    {"inputgain", "Input", 485, KnobGroup::echo},
};
const TogglePlace kToggles[] = {
    {"poly", "POLY", "Polyphony", 175, 418, KnobGroup::loop}, {"polylock", "LOCK", "String Lock", 245, 418, KnobGroup::loop},
    {"midipitch", "PITCH", "MIDI Pitch", 705, 456, KnobGroup::echo}, {"midigate", "GATE", "MIDI Gate", 785, 456, KnobGroup::echo},
};
constexpr const char* kStringKnobs[] = {"coupling", "interval2", "interval3"};

float panelY(float windowY) { return windowY - (float) SetupPanel::kTop; }
} // namespace

SetupPanel::SetupPanel(juce::AudioProcessorValueTreeState& apvts) {
    for (const auto& place : kKnobs) {
        auto* parameter = apvts.getParameter(place.id);
        jassert(parameter != nullptr);
        knobs_[place.id] = std::make_unique<LabeledKnob>(*parameter, place.caption, KnobSize::small, place.group);
        addAndMakeVisible(*knobs_[place.id]);
    }
    for (const auto& place : kToggles) {
        auto toggle = std::make_unique<Toggle>();
        toggle->button.setButtonText(place.caption);
        toggle->button.setTitle(place.title);
        toggle->button.setClickingTogglesState(true);
        toggle->button.getProperties().set(DroseraLookAndFeel::kGroup, place.group == KnobGroup::loop ? "loop" : "echo");
        auto* parameter = apvts.getParameter(place.id);
        jassert(parameter != nullptr);
        toggle->attachment = std::make_unique<juce::ButtonParameterAttachment>(*parameter, toggle->button, nullptr);
        addAndMakeVisible(toggle->button);
        toggles_[place.id] = std::move(toggle);
    }
}

juce::Button* SetupPanel::toggle(const juce::String& paramId) {
    const auto found = toggles_.find(paramId);
    return found != toggles_.end() ? &found->second->button : nullptr;
}

void SetupPanel::refresh(bool polyOn) {
    for (const char* id : kStringKnobs) knobs_[id]->setDimmed(!polyOn);
}

void SetupPanel::paint(juce::Graphics& g) {
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1b1315), 0.0f, 0.0f, juce::Colour(0xff140e10), 0.0f, (float) getHeight(), false));
    g.fillAll();
    g.setColour(theme::hairline());
    g.drawHorizontalLine(0, 0.0f, (float) getWidth());
    drawCartouche(g, "STRINGS", 100.0f, 320.0f, panelY(392.0f));
    drawCartouche(g, "OUTPUT", 375.0f, 525.0f, panelY(392.0f));
    drawCartouche(g, "MIDI", 660.0f, 830.0f, panelY(392.0f));
}

void SetupPanel::resized() {
    const int top = kKnobY - kTop - LabeledKnob::radius(KnobSize::small) - LabeledKnob::margin(KnobSize::small);
    for (const auto& place : kKnobs) knobs_[place.id]->setBounds(place.x - kCellWidth / 2, top, kCellWidth, kCellHeight);
    for (const auto& place : kToggles)
        toggles_[place.id]->button.setBounds(juce::Rectangle<int>(56, 22).withCentre({place.x, place.y - kTop}));
}

} // namespace drosera
