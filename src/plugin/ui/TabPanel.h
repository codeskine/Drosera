#pragma once
// The tab strip with the visible tab's controls.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include "LabeledKnob.h"

namespace drosera {

/// Tabs are named by an ID that projects save, not by their place in the strip: PONG has ID 6 though it follows ECHO,
/// so a project saved on OUTPUT, ID 3, still opens it.
class TabPanel final : public juce::Component {
public:
    static constexpr int kNumTabs = 7;

    explicit TabPanel(juce::AudioProcessorValueTreeState&);

    void setTab(int id);
    int tab() const { return tab_; }
    std::function<void(int id)> onTabClicked;
    juce::Rectangle<int> tabBounds(int id) const { return tabButtons_[place(id)].getBounds(); }
    juce::Rectangle<int> toggleBounds(const juce::String& paramId) const { return toggles_.at(paramId)->button.getBounds(); }

    /// The knobs in every tab, by parameter ID.
    const std::map<juce::String, std::unique_ptr<LabeledKnob>>& knobs() const { return knobs_; }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Page {
        int id;
        juce::String title;
        std::vector<juce::String> knobs, toggles;
    };

    /// The tab's place in the strip, or 0 for an ID no tab has.
    int place(int id) const;
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
