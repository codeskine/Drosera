#pragma once
// Custom editor for Drosera: controls grouped by signal path, in the order of the
// original panel (string -> filters -> reverb -> echo -> output) plus the plugin-only
// input level and MIDI switches.

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

class DroseraLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    DroseraLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos,
                          float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;

    static juce::Colour accent() { return juce::Colour(0xff8be05a); }
    static juce::Colour accentDim() { return juce::Colour(0xff35522a); }
    static juce::Colour bgTop() { return juce::Colour(0xff0b110d); }
    static juce::Colour bgBottom() { return juce::Colour(0xff1c2b1e); }
    static juce::Colour panel() { return juce::Colour(0x1affffff); }
    static juce::Colour text() { return juce::Colour(0xffd9e6d4); }
    static juce::Colour textDim() { return juce::Colour(0xff8aa085); }
};

class DroseraEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    DroseraEditor(juce::AudioProcessor&, juce::AudioProcessorValueTreeState&, std::function<bool()> isMidiPitchActive);
    ~DroseraEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void selectProgram(int index);

    struct Knob {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::SliderParameterAttachment> attachment;
    };
    struct Toggle {
        juce::ToggleButton button;
        std::unique_ptr<juce::ButtonParameterAttachment> attachment;
    };
    struct Section {
        juce::String title;
        std::vector<int> knobs;    // indices into knobs_
        std::vector<int> toggles;  // indices into toggles_
        juce::Rectangle<int> bounds;
    };

    int addKnob(juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& caption);
    int addToggle(juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& caption);

    DroseraLookAndFeel laf_;
    std::function<bool()> isMidiPitchActive_;
    bool showingMidiPitch_ = false;
    std::atomic<float>* echoTape_ = nullptr;
    int wowKnob_ = 0, ageKnob_ = 0;
    juce::ComboBox presetBox_;
    juce::TextButton prevButton_ {"<"}, nextButton_ {">"};
    std::vector<std::unique_ptr<Knob>> knobs_;
    std::vector<std::unique_ptr<Toggle>> toggles_;
    std::vector<Section> sections_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DroseraEditor)
};
