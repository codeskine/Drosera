#pragma once
// The setup panel, rising over the bottom of the window: Strings, Output and MIDI, which are set rather than played.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include "LabeledKnob.h"

namespace drosera {

class SetupPanel final : public juce::Component {
public:
    /// The panel's top in the window. Its children are laid out from the mockup's window coordinates.
    static constexpr int kTop = 368;

    explicit SetupPanel(juce::AudioProcessorValueTreeState&);

    const std::map<juce::String, std::unique_ptr<LabeledKnob>>& knobs() const { return knobs_; }
    /// POLY, LOCK, PITCH or GATE, or nullptr for another ID.
    juce::Button* toggle(const juce::String& paramId);

    /// Message thread: dims Coupling and the intervals while POLY is off.
    void refresh(bool polyOn);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Toggle {
        juce::ToggleButton button;
        std::unique_ptr<juce::ButtonParameterAttachment> attachment;
    };

    std::map<juce::String, std::unique_ptr<LabeledKnob>> knobs_;
    std::map<juce::String, std::unique_ptr<Toggle>> toggles_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SetupPanel)
};

} // namespace drosera
