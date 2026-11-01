#pragma once
// A knob with its name and readout, and the tab strip with the visible tab's controls.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>

namespace drosera {

class LabeledKnob final : public juce::Component {
public:
    LabeledKnob(juce::RangedAudioParameter&, const juce::String& caption, bool mainControl);

    juce::Slider& slider() { return slider_; }
    juce::RangedAudioParameter& parameter() { return parameter_; }
    /// Dims the knob while what sounds is not its position. It stays usable: moving it can take control back.
    void show(const juce::String& value, const juce::String& tag);
    juce::String readout() const { return value_.getText(); }
    juce::String tag() const { return tag_.getText(); }

    void resized() override;

private:
    juce::RangedAudioParameter& parameter_;
    juce::Slider slider_;
    juce::Label caption_, value_, tag_;
    juce::SliderParameterAttachment attachment_;
};

class TabPanel final : public juce::Component {
public:
    static constexpr int kNumTabs = 5;

    explicit TabPanel(juce::AudioProcessorValueTreeState&);

    void setTab(int);
    int tab() const { return tab_; }
    std::function<void(int)> onTabClicked;

    /// The knobs in every tab, by parameter ID.
    const std::map<juce::String, std::unique_ptr<LabeledKnob>>& knobs() const { return knobs_; }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Page {
        juce::String title;
        std::vector<juce::String> knobs, toggles;
    };
    struct Toggle {
        juce::ToggleButton button;
        std::unique_ptr<juce::ButtonParameterAttachment> attachment;
    };

    const std::vector<Page> pages_;
    int tab_ = 0;
    std::map<juce::String, std::unique_ptr<LabeledKnob>> knobs_;
    std::map<juce::String, std::unique_ptr<Toggle>> toggles_;
    juce::TextButton tabButtons_[kNumTabs];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TabPanel)
};

} // namespace drosera
