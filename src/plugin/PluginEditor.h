#pragma once
// Drosera's window: assembles the header, the plant, the main knobs, the row of controls and the setup panel, and runs
// the timer that keeps them honest.

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include "EditorBridge.h"
#include "ui/ControlRow.h"
#include "ui/DroseraLookAndFeel.h"
#include "ui/HeaderBar.h"
#include "ui/LabeledKnob.h"
#include "ui/PlantView.h"
#include "ui/SetupPanel.h"

class DroseraEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    DroseraEditor(juce::AudioProcessor&, juce::AudioProcessorValueTreeState&, EditorBridge&);
    ~DroseraEditor() override;

    void selectProgram(int index);
    void setSetupOpen(bool);
    bool setupOpen() const { return setup_.isVisible(); }
    /// The window's size over its 900×560 design.
    float scale() const { return (float) getWidth() / 900.0f; }

    /// The timer's body: moves the plant on by dtSeconds and refreshes every readout.
    void refreshDisplay(float dtSeconds);

    juce::Slider* slider(const juce::String& paramId);
    juce::Button* toggle(const juce::String& paramId);
    juce::String readout(const juce::String& paramId) const;
    juce::String tag(const juce::String& paramId) const;
    const drosera::PlantView& plant() const { return plant_; }
    drosera::HeaderBar& header() { return header_; }

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    /// Dims the window above the open setup panel; a click on it closes the panel.
    struct Scrim final : juce::Component {
        std::function<void()> onClick;
        void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0x990b0809)); }
        void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }
    };

    void timerCallback() override;
    /// Places every child in the 900×560 content, which never changes size: the window scales it.
    void layOut();
    const drosera::LabeledKnob* findKnob(const juce::String& paramId) const;

    drosera::DroseraLookAndFeel lookAndFeel_;
    juce::AudioProcessorValueTreeState& apvts_;
    EditorBridge& bridge_;
    drosera::PlantView plant_;
    juce::SliderParameterAttachment feedbackAttachment_;
    juce::Component content_;  // everything, laid out at 900×560 and scaled as a whole
    drosera::LabeledKnob frequency_, body_;
    drosera::ControlRow row_;
    Scrim scrim_;
    drosera::SetupPanel setup_;
    drosera::HeaderBar header_;
    double lastTick_ = 0.0, pulseSeconds_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DroseraEditor)
};
