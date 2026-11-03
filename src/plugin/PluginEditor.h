#pragma once
// Drosera's window: assembles the plant, the main knobs and the tabs, and runs the timer that keeps them honest.

#include <juce_audio_processors/juce_audio_processors.h>
#include "EditorBridge.h"
#include "ui/DroseraLookAndFeel.h"
#include "ui/PlantView.h"
#include "ui/TabPanel.h"

class DroseraEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    DroseraEditor(juce::AudioProcessor&, juce::AudioProcessorValueTreeState&, EditorBridge&);
    ~DroseraEditor() override;

    void setTab(int);
    void selectProgram(int index);
    int tab() const { return tabs_.tab(); }
    juce::Rectangle<int> tabBounds(int index) const { return tabs_.tabBounds(index).translated(tabs_.getX(), tabs_.getY()); }

    /// The timer's body: moves the plant on by dtSeconds and refreshes every readout.
    void refreshDisplay(float dtSeconds);

    juce::Slider* slider(const juce::String& paramId);
    juce::String readout(const juce::String& paramId) const;
    juce::String tag(const juce::String& paramId) const;
    const drosera::PlantView& plant() const { return plant_; }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    const drosera::LabeledKnob* findKnob(const juce::String& paramId) const;

    drosera::DroseraLookAndFeel lookAndFeel_;
    juce::AudioProcessorValueTreeState& apvts_;
    EditorBridge& bridge_;
    drosera::PlantView plant_;
    juce::SliderParameterAttachment feedbackAttachment_;
    drosera::LabeledKnob frequency_, body_;
    drosera::TabPanel tabs_;
    juce::ComboBox presetBox_;
    juce::TextButton previous_ {juce::String(juce::CharPointer_UTF8("\xe2\x80\xb9"))}, next_ {juce::String(juce::CharPointer_UTF8("\xe2\x80\xba"))};
    double lastTick_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DroseraEditor)
};
