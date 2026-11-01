#include "PluginEditor.h"
#include "ui/Readouts.h"
#include "ui/Theme.h"

namespace {
constexpr int kWidth = 760, kHeight = 500, kTabsTop = 306, kPlantHeight = 314;
constexpr auto kTabProperty = "uiTab";
} // namespace

DroseraEditor::DroseraEditor(juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& apvts, EditorBridge& bridge)
    : AudioProcessorEditor(p), apvts_(apvts), bridge_(bridge),
      feedbackAttachment_(*apvts.getParameter("fbgain"), plant_.feedback(), nullptr),
      frequency_(*apvts.getParameter("freq"), "Frequency", true),
      body_(*apvts.getParameter("body"), "Body", true),
      tabs_(apvts) {
    setLookAndFeel(&lookAndFeel_);
    addAndMakeVisible(plant_);
    addAndMakeVisible(frequency_);
    addAndMakeVisible(body_);
    addAndMakeVisible(tabs_);

    tabs_.onTabClicked = [this](int index) { setTab(index); };
    tabs_.setTab(apvts_.state.getProperty(kTabProperty, 0));

    for (int i = 0; i < p.getNumPrograms(); ++i) presetBox_.addItem(p.getProgramName(i), i + 1);
    presetBox_.setSelectedId(p.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox_.onChange = [this] { selectProgram(presetBox_.getSelectedId() - 1); };
    addAndMakeVisible(presetBox_);
    previous_.onClick = [this] { selectProgram((processor.getCurrentProgram() + processor.getNumPrograms() - 1) % processor.getNumPrograms()); };
    next_.onClick = [this] { selectProgram((processor.getCurrentProgram() + 1) % processor.getNumPrograms()); };
    addAndMakeVisible(previous_);
    addAndMakeVisible(next_);

    setResizable(false, false);
    setSize(kWidth, kHeight);

    bridge_.spectrumTap().setActive(true);
    refreshDisplay(0.0f);
    lastTick_ = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(30);
}

DroseraEditor::~DroseraEditor() {
    stopTimer();
    bridge_.spectrumTap().setActive(false);
    setLookAndFeel(nullptr);
}

void DroseraEditor::setTab(int index) {
    tabs_.setTab(index);
    apvts_.state.setProperty(kTabProperty, tabs_.tab(), nullptr);
}

void DroseraEditor::selectProgram(int index) {
    if (!juce::isPositiveAndBelow(index, processor.getNumPrograms())) return;
    processor.setCurrentProgram(index);
    presetBox_.setSelectedId(index + 1, juce::dontSendNotification);
}

void DroseraEditor::timerCallback() {
    const double now = juce::Time::getMillisecondCounterHiRes();
    refreshDisplay((float) juce::jlimit(0.0, 0.25, (now - lastTick_) * 0.001));
    lastTick_ = now;
}

void DroseraEditor::refreshDisplay(float dtSeconds) {
    plant_.update(bridge_.spectrumTap(), bridge_.hostSampleRate(), dtSeconds);
    plant_.setFeedbackText(apvts_.getParameter("fbgain")->getCurrentValueAsText());

    const bool midi = bridge_.midiPitchActive();
    const auto frequency = drosera::readouts::frequency(midi, bridge_.soundingNote(), frequency_.parameter().getCurrentValueAsText().toStdString());
    const auto body = drosera::readouts::body(midi, bridge_.bodySeconds(), body_.parameter().getCurrentValueAsText().toStdString());
    frequency_.show(frequency.value, frequency.tag);
    body_.show(body.value, body.tag);

    const bool warp = apvts_.getRawParameterValue("echohalve")->load() > 0.5f;
    for (const auto& [id, knob] : tabs_.knobs()) {
        const auto text = knob->parameter().getCurrentValueAsText();
        knob->show(id == "echotime" ? juce::String(drosera::readouts::echoTime(warp, bridge_.echoSeconds(), text.toStdString())) : text, {});
    }

    // Follow what the host changes.
    const int program = processor.getCurrentProgram() + 1;
    if (presetBox_.getSelectedId() != program) presetBox_.setSelectedId(program, juce::dontSendNotification);
    const int storedTab = apvts_.state.getProperty(kTabProperty, 0);
    if (storedTab != tabs_.tab() && juce::isPositiveAndBelow(storedTab, drosera::TabPanel::kNumTabs)) tabs_.setTab(storedTab);
}

const drosera::LabeledKnob* DroseraEditor::findKnob(const juce::String& paramId) const {
    if (paramId == "freq") return &frequency_;
    if (paramId == "body") return &body_;
    const auto found = tabs_.knobs().find(paramId);
    return found != tabs_.knobs().end() ? found->second.get() : nullptr;
}

juce::Slider* DroseraEditor::slider(const juce::String& paramId) {
    if (paramId == "fbgain") return &plant_.feedback();
    auto* knob = const_cast<drosera::LabeledKnob*>(findKnob(paramId));
    return knob != nullptr ? &knob->slider() : nullptr;
}

juce::String DroseraEditor::readout(const juce::String& paramId) const {
    const auto* knob = findKnob(paramId);
    return knob != nullptr ? knob->readout() : juce::String();
}

juce::String DroseraEditor::tag(const juce::String& paramId) const {
    const auto* knob = findKnob(paramId);
    return knob != nullptr ? knob->tag() : juce::String();
}

void DroseraEditor::paint(juce::Graphics& g) {
    g.fillAll(drosera::theme::background());
    g.setColour(drosera::theme::text());
    g.setFont(drosera::theme::titleFont());
    g.drawText("DROSERA", 26, 18, 260, 28, juce::Justification::centredLeft);
}

void DroseraEditor::resized() {
    plant_.setBounds(0, 0, kWidth, kPlantHeight);
    frequency_.setBounds(135 - 60, 132, 120, 138);
    body_.setBounds(625 - 60, 132, 120, 138);
    tabs_.setBounds(0, kTabsTop, kWidth, kHeight - kTabsTop);

    previous_.setBounds(kWidth - 26 - 220, 20, 24, 24);
    presetBox_.setBounds(kWidth - 26 - 196, 20, 172, 24);
    next_.setBounds(kWidth - 26 - 24, 20, 24, 24);
}
