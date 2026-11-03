#pragma once
// The header: the PRESET field on the left, the name in the centre, and the SETUP field that opens the setup panel.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace drosera {

class HeaderBar final : public juce::Component {
public:
    static constexpr int kHeight = 70;

    explicit HeaderBar(juce::AudioProcessor&);

    std::function<void(int index)> onProgramChosen;
    std::function<void()> onSetupClicked;

    void setProgram(int index);
    void setSetupOpen(bool);

    juce::Button& previousButton() { return previous_; }
    juce::Button& nextButton() { return next_; }
    juce::Button& presetButton() { return preset_; }
    juce::Button& setupButton() { return setup_; }

    /// Every preset, the current one ticked. Item IDs are the program's index plus one.
    juce::PopupMenu presetMenu() const;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    /// A click target: HeaderBar draws the fields themselves.
    struct Area final : juce::Button {
        explicit Area(const juce::String& title) : juce::Button(title) { setTitle(title); }
        void paintButton(juce::Graphics&, bool, bool) override {}
    };

    void choose(int index);

    juce::AudioProcessor& processor_;
    Area previous_ {"Previous preset"}, next_ {"Next preset"}, preset_ {"Preset"}, setup_ {"Strings, Output and MIDI"};
    int program_ = 0;
    bool setupOpen_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HeaderBar)
};

} // namespace drosera
