#include "TabPanel.h"
#include "DroseraLookAndFeel.h"
#include "Theme.h"

namespace drosera {

namespace {
// In the panel's coordinates. The panel starts 306 px down the window.
constexpr int kStripTop = 8, kStripHeight = 22, kLineY = 32;
constexpr int kTabStep = 101, kTabWidth = 91, kMargin = 26;
constexpr int kCellTop = 48, kCellWidth = 92, kCellHeight = 92, kCellStep = 112;
constexpr int kToggleWidth = 78, kToggleHeight = 22;

const std::map<juce::String, juce::String>& captions() {
    static const std::map<juce::String, juce::String> names {
        {"lpf", "Lowpass"}, {"hpf", "Highpass"}, {"revmix", "Mix"}, {"revdecay", "Decay"}, {"echosend", "Send"},
        {"echotime", "Time"}, {"echofb", "Feedback"}, {"volume", "Volume"}, {"inputgain", "Input"},
        {"echodrive", "Drive"}, {"echotone", "Tone"}, {"echowidth", "Width"}, {"echolevel", "Level"},
        {"echohalve", "WARP"}, {"echopong", "PONG"}, {"midipitch", "PITCH"}, {"midigate", "GATE"},
        {"coupling", "Coupling"}, {"interval2", "Interval 2"}, {"interval3", "Interval 3"}, {"poly", "POLY"}, {"polylock", "LOCK"},
    };
    return names;
}
} // namespace

TabPanel::TabPanel(juce::AudioProcessorValueTreeState& apvts)
    : pages_ {{0, "FILTERS", {"lpf", "hpf"}, {}},
              {1, "REVERB", {"revmix", "revdecay"}, {}},
              {2, "ECHO", {"echosend", "echotime", "echofb"}, {"echohalve"}},
              {6, "PONG", {"echodrive", "echotone", "echowidth", "echolevel"}, {"echopong"}},
              {3, "OUTPUT", {"volume", "inputgain"}, {}},
              {4, "MIDI", {}, {"midipitch", "midigate"}},
              {5, "STRINGS", {"coupling", "interval2", "interval3"}, {"poly", "polylock"}}} {
    for (int i = 0; i < kNumTabs; ++i) {
        auto& page = pages_[(size_t) i];
        auto& button = tabButtons_[i];
        button.setButtonText(page.title);
        button.onClick = [this, id = page.id] { if (onTabClicked) onTabClicked(id); };
        addAndMakeVisible(button);

        for (const auto& id : page.knobs) {
            auto* parameter = apvts.getParameter(id);
            jassert(parameter != nullptr);
            const bool loop = id == "lpf" || id == "hpf" || id == "revmix" || id == "revdecay" || id == "coupling" || id.startsWith("interval");
            knobs_[id] = std::make_unique<LabeledKnob>(*parameter, captions().at(id), KnobSize::small, loop ? KnobGroup::loop : KnobGroup::echo);
            addChildComponent(*knobs_[id]);
        }
        for (const auto& id : page.toggles) {
            auto toggle = std::make_unique<Toggle>();
            toggle->button.setButtonText(captions().at(id));
            toggle->button.setClickingTogglesState(true);
            auto* parameter = apvts.getParameter(id);
            jassert(parameter != nullptr);
            toggle->attachment = std::make_unique<juce::ButtonParameterAttachment>(*parameter, toggle->button, nullptr);
            addChildComponent(toggle->button);
            toggles_[id] = std::move(toggle);
        }
    }
    setTab(0);
}

int TabPanel::place(int id) const {
    for (int i = 0; i < kNumTabs; ++i)
        if (pages_[(size_t) i].id == id) return i;
    return 0;
}

void TabPanel::setTab(int id) {
    tab_ = pages_[(size_t) place(id)].id;  // a damaged project opens the first tab
    for (int i = 0; i < kNumTabs; ++i) {
        const bool open = pages_[(size_t) i].id == tab_;
        tabButtons_[i].setToggleState(open, juce::dontSendNotification);
        for (const auto& paramId : pages_[(size_t) i].knobs) knobs_[paramId]->setVisible(open);
        for (const auto& paramId : pages_[(size_t) i].toggles) toggles_[paramId]->button.setVisible(open);
    }
    repaint();
}

void TabPanel::paint(juce::Graphics& g) {
    g.setColour(theme::track());
    g.fillRect(kMargin, kLineY, getWidth() - 2 * kMargin, 1);
    g.setColour(theme::echo());
    g.fillRect(kMargin + place(tab_) * kTabStep, kLineY - 2, kTabWidth, 3);
}

void TabPanel::resized() {
    for (int i = 0; i < kNumTabs; ++i)
        tabButtons_[i].setBounds(kMargin + i * kTabStep, kStripTop, kTabWidth, kStripHeight);

    for (const auto& page : pages_) {
        const int count = (int) (page.knobs.size() + page.toggles.size());
        const int step = count > 1 ? juce::jmin(kCellStep, (getWidth() - 2 * kMargin - kCellWidth) / (count - 1)) : kCellStep;
        int x = (getWidth() - (count - 1) * step) / 2;
        for (const auto& id : page.knobs) {
            knobs_[id]->setBounds(x - kCellWidth / 2, kCellTop, kCellWidth, kCellHeight);
            x += step;
        }
        const int top = kCellTop + (kCellHeight - kToggleHeight) / 2 - 8;
        for (const auto& id : page.toggles) {
            toggles_[id]->button.setBounds(x - kToggleWidth / 2, top, kToggleWidth, kToggleHeight);
            x += step;
        }
    }
}

} // namespace drosera
