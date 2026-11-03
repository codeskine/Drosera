#pragma once
// A knob with its name and readout, drawn in its group's colour.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace drosera {

enum class KnobSize { large, small, mini };
/// Loop knobs change what the drone sustains, echo knobs colour what comes out of it.
enum class KnobGroup { loop, echo };

class LabeledKnob final : public juce::Component {
public:
    LabeledKnob(juce::RangedAudioParameter&, const juce::String& caption, KnobSize, KnobGroup);

    static int radius(KnobSize size) { return size == KnobSize::large ? 48 : size == KnobSize::small ? 20 : 18; }
    /// The space the knob's drawing needs around its radius: the large knobs carry a scale.
    static int margin(KnobSize size) { return size == KnobSize::large ? 16 : 4; }

    juce::Slider& slider() { return slider_; }
    juce::RangedAudioParameter& parameter() { return parameter_; }
    /// Dims the knob while what sounds is not its position. It stays usable: moving it can take control back.
    void show(const juce::String& value, const juce::String& tag);
    /// Dims the whole knob while it has no effect, such as PONG's knobs in CLASSIC. It stays usable.
    void setDimmed(bool);
    juce::String readout() const { return value_.getText(); }
    juce::String tag() const { return tag_.getText(); }

    /// Where the knob's centre sits in this component, for laying it out by centre.
    juce::Point<int> knobCentre() const;

    void resized() override;

private:
    juce::RangedAudioParameter& parameter_;
    const KnobSize size_;
    juce::Slider slider_;
    juce::Label caption_, value_, tag_;
    juce::SliderParameterAttachment attachment_;
};

} // namespace drosera
