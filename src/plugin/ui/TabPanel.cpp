#include "TabPanel.h"
#include "DroseraLookAndFeel.h"
#include "Theme.h"

namespace drosera {

namespace {
// In the panel's coordinates. The panel starts 306 px down the window.
constexpr int kStripTop = 8, kStripHeight = 22, kLineY = 32;
constexpr int kTabStep = 118, kTabWidth = 108, kMargin = 26;
constexpr int kCellTop = 48, kCellWidth = 92, kCellHeight = 92, kCellStep = 112;
constexpr int kToggleWidth = 78, kToggleHeight = 22;

juce::Label& style(juce::Label& label, const juce::Font& font, juce::Colour colour) {
    label.setFont(font);
    label.setColour(juce::Label::textColourId, colour);
    label.setJustificationType(juce::Justification::centred);
    label.setInterceptsMouseClicks(false, false);
    label.setBorderSize({});
    return label;
}

const std::map<juce::String, juce::String>& captions() {
    static const std::map<juce::String, juce::String> names {
        {"lpf", "Lowpass"}, {"hpf", "Highpass"}, {"revmix", "Mix"}, {"revdecay", "Decay"}, {"echosend", "Send"},
        {"echotime", "Time"}, {"echofb", "Feedback"}, {"volume", "Volume"}, {"inputgain", "Input"},
        {"echohalve", "WARP"}, {"midipitch", "PITCH"}, {"midigate", "GATE"},
        {"coupling", "Coupling"}, {"interval2", "Interval 2"}, {"interval3", "Interval 3"}, {"poly", "POLY"}, {"polylock", "LOCK"},
    };
    return names;
}
} // namespace

LabeledKnob::LabeledKnob(juce::RangedAudioParameter& parameter, const juce::String& caption, bool mainControl)
    : parameter_(parameter), attachment_(parameter, slider_, nullptr) {
    slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider_.setRotaryParameters(juce::degreesToRadians(225.0f), juce::degreesToRadians(495.0f), true);
    slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider_.setTitle(caption);
    if (mainControl) slider_.getProperties().set(DroseraLookAndFeel::kMainControl, true);
    addAndMakeVisible(slider_);

    caption_.setText(caption, juce::dontSendNotification);
    addAndMakeVisible(style(caption_, theme::labelFont(), theme::text()));
    addAndMakeVisible(style(value_, theme::valueFont(), theme::text().withAlpha(0.8f)));
    addAndMakeVisible(style(tag_, theme::tagFont(), theme::dew()));
}

void LabeledKnob::show(const juce::String& value, const juce::String& tag) {
    value_.setText(value, juce::dontSendNotification);
    tag_.setText(tag, juce::dontSendNotification);
    slider_.setAlpha(tag.isEmpty() ? 1.0f : 0.45f);
}

void LabeledKnob::resized() {
    auto r = getLocalBounds();
    tag_.setBounds(r.removeFromBottom(13));
    value_.setBounds(r.removeFromBottom(14));
    caption_.setBounds(r.removeFromBottom(16));
    slider_.setBounds(r.withSizeKeepingCentre(juce::jmin(r.getWidth(), r.getHeight()), juce::jmin(r.getWidth(), r.getHeight())));
}

TabPanel::TabPanel(juce::AudioProcessorValueTreeState& apvts)
    : pages_ {{"FILTERS", {"lpf", "hpf"}, {}},
              {"REVERB", {"revmix", "revdecay"}, {}},
              {"ECHO", {"echosend", "echotime", "echofb"}, {"echohalve"}},
              {"OUTPUT", {"volume", "inputgain"}, {}},
              {"MIDI", {}, {"midipitch", "midigate"}},
              {"STRINGS", {"coupling", "interval2", "interval3"}, {"poly", "polylock"}}} {
    for (int i = 0; i < kNumTabs; ++i) {
        auto& page = pages_[(size_t) i];
        auto& button = tabButtons_[i];
        button.setButtonText(page.title);
        button.onClick = [this, i] { if (onTabClicked) onTabClicked(i); };
        addAndMakeVisible(button);

        for (const auto& id : page.knobs) {
            auto* parameter = apvts.getParameter(id);
            jassert(parameter != nullptr);
            knobs_[id] = std::make_unique<LabeledKnob>(*parameter, captions().at(id), false);
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

void TabPanel::setTab(int index) {
    tab_ = juce::isPositiveAndBelow(index, kNumTabs) ? index : 0;  // a damaged project opens the first tab
    for (int i = 0; i < kNumTabs; ++i) {
        const bool open = i == tab_;
        tabButtons_[i].setToggleState(open, juce::dontSendNotification);
        for (const auto& id : pages_[(size_t) i].knobs) knobs_[id]->setVisible(open);
        for (const auto& id : pages_[(size_t) i].toggles) toggles_[id]->button.setVisible(open);
    }
    repaint();
}

void TabPanel::paint(juce::Graphics& g) {
    g.setColour(theme::track());
    g.fillRect(kMargin, kLineY, getWidth() - 2 * kMargin, 1);
    g.setColour(theme::dew());
    g.fillRect(kMargin + tab_ * kTabStep, kLineY - 2, kTabWidth, 3);
}

void TabPanel::resized() {
    for (int i = 0; i < kNumTabs; ++i)
        tabButtons_[i].setBounds(kMargin + i * kTabStep, kStripTop, kTabWidth, kStripHeight);

    for (const auto& page : pages_) {
        const int count = (int) (page.knobs.size() + page.toggles.size());
        int x = (getWidth() - (count - 1) * kCellStep) / 2;
        for (const auto& id : page.knobs) {
            knobs_[id]->setBounds(x - kCellWidth / 2, kCellTop, kCellWidth, kCellHeight);
            x += kCellStep;
        }
        const int top = kCellTop + (kCellHeight - kToggleHeight) / 2 - 8;
        for (const auto& id : page.toggles) {
            toggles_[id]->button.setBounds(x - kToggleWidth / 2, top, kToggleWidth, kToggleHeight);
            x += kCellStep;
        }
    }
}

} // namespace drosera
