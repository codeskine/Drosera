#pragma once
// The row under the plant: FILTER and REVERB on the left, PONG in the centre, ECHO on the right. Everything played live
// that is not Frequency, Body or Feedback.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include "EchoPulse.h"
#include "LabeledKnob.h"
#include "Pill.h"
#include "ResponseArcs.h"

namespace drosera {

class ControlRow final : public juce::Component {
public:
    /// The row's top in the window. Its children are laid out from the mockup's window coordinates.
    static constexpr int kTop = 400;

    explicit ControlRow(juce::AudioProcessorValueTreeState&);

    const std::map<juce::String, std::unique_ptr<LabeledKnob>>& knobs() const { return knobs_; }
    /// The CLASSIC / PONG switch and WARP, or nullptr for another ID.
    juce::Button* toggle(const juce::String& paramId);

    /// Message thread: redraws the arcs, dims PONG's knobs in CLASSIC and lights the echo pulse.
    void refresh(float highpassHz, float lowpassHz, float toneHz, bool pong, pulse::Lit);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    std::map<juce::String, std::unique_ptr<LabeledKnob>> knobs_;
    ModeSwitch mode_;
    juce::ToggleButton warp_ {"WARP"};
    juce::ButtonParameterAttachment modeAttachment_, warpAttachment_;
    float bandTicks_[arcs::kBandTicks] {}, toneTicks_[arcs::kToneTicks] {};
    bool pong_ = false;
    pulse::Lit lit_ = pulse::Lit::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlRow)
};

} // namespace drosera
